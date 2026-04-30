// sckbackend.mm — ScreenCaptureKit capture backend (macOS 12.3+)
//
// Compile only when targeting macOS; the CMakeLists guard is:
//   if(APPLE) list(APPEND SOURCES src/platform/sckbackend.mm) endif()
//
// ScreenCaptureKit API reference:
//   SCShareableContent  — async enumeration of displays, windows, apps
//   SCContentFilter     — what to capture / exclude
//   SCStream            — streaming capture session
//   SCStreamOutput      — protocol; receives CMSampleBuffer per frame
//   SCStreamDelegate    — protocol; receives stream errors / stop events

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "sckbackend.hpp"
#include "macos_window.h"

#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>
#include <QScreen>

#include <dispatch/dispatch.h>

// ---------------------------------------------------------------------------
// Forward-declare the Obj-C delegate so SckState can reference it.
// ---------------------------------------------------------------------------
@class SckDelegate;

// ---------------------------------------------------------------------------
// SckState — Obj-C objects in a plain struct so the .hpp stays pure C++.
// Guarded by SckScreenCaptureBackend::m_stateMutex.
// ---------------------------------------------------------------------------
namespace sc {
struct SckState {
    __strong SCStream*    stream   = nil;
    __strong SckDelegate* delegate = nil;
};
} // namespace sc

// ---------------------------------------------------------------------------
// Obj-C delegate — implements SCStreamOutput + SCStreamDelegate.
// Holds a raw pointer back to the Qt backend; call invalidate() before
// the backend is destroyed.
// ---------------------------------------------------------------------------
API_AVAILABLE(macos(12.3))
@interface SckDelegate : NSObject <SCStreamDelegate, SCStreamOutput>
- (instancetype)initWithBackend:(sc::SckScreenCaptureBackend*)backend;
- (void)invalidate;
@end

@implementation SckDelegate {
    sc::SckScreenCaptureBackend* _backend;   // weak; call invalidate() before destruction
}

- (instancetype)initWithBackend:(sc::SckScreenCaptureBackend*)backend {
    if ((self = [super init]))
        _backend = backend;
    return self;
}

- (void)invalidate {
    _backend = nullptr;
}

// SCStreamOutput — called on SCK's internal dispatch queue.
- (void)stream:(SCStream*)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
    ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeScreen) return;

    sc::SckScreenCaptureBackend* backend = _backend;
    if (!backend || !backend->isRunning()) return;

    CVImageBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixelBuffer) return;

    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

    const uint8_t* base   = (const uint8_t*)CVPixelBufferGetBaseAddress(pixelBuffer);
    const int      width  = (int)CVPixelBufferGetWidth(pixelBuffer);
    const int      height = (int)CVPixelBufferGetHeight(pixelBuffer);
    const int      stride = (int)CVPixelBufferGetBytesPerRow(pixelBuffer);

    static std::atomic<int> s_logged{0};
    if (s_logged.fetch_add(1) < 3) {
        qDebug("[SCK] frame %d: pixelbuffer %dx%d stride=%d fmt=%u",
               s_logged.load(),
               width, height, stride,
               (unsigned)CVPixelBufferGetPixelFormatType(pixelBuffer));
    }

    // kCVPixelFormatType_32BGRA → BGRA in memory = QImage::Format_ARGB32 on LE ARM/x86.
    QImage img(base, width, height, stride, QImage::Format_ARGB32);
    QImage copy = img.copy();   // detach from pixel buffer memory before unlock

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

    // Post to backend's thread (the worker thread) via queued connection.
    // 'backend' is captured by value; it remains valid because stopCapture()
    // blocks until the stream is stopped before the backend is destroyed.
    QMetaObject::invokeMethod(backend, [backend, copy = std::move(copy)]() mutable {
        if (backend->isRunning())
            emit backend->frameArrived(copy);
    }, Qt::QueuedConnection);
}

// SCStreamDelegate — called when the stream stops due to an error.
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    sc::SckScreenCaptureBackend* backend = _backend;
    if (!backend || !error) return;
    QString msg = QString::fromNSString(error.localizedDescription);
    QMetaObject::invokeMethod(backend, [backend, msg]() {
        emit backend->errorOccurred(msg);
    }, Qt::QueuedConnection);
}

@end

// ---------------------------------------------------------------------------
// Helper: get CGDirectDisplayID for a QScreen
// ---------------------------------------------------------------------------
static CGDirectDisplayID displayIdForQScreen(QScreen* screen)
{
    if (!screen) return CGMainDisplayID();

    const QRect qr = screen->geometry();
    for (NSScreen* ns in [NSScreen screens]) {
        const NSRect f = ns.frame;
        // Match by size (tolerant of 1-pixel rounding differences).
        if (qAbs((int)f.size.width  - qr.width())  <= 1 &&
            qAbs((int)f.size.height - qr.height()) <= 1) {
            NSNumber* n = ns.deviceDescription[@"NSScreenNumber"];
            if (n) return [n unsignedIntValue];
        }
    }
    qWarning("[SCK] could not match QScreen to CGDirectDisplayID, using main display");
    return CGMainDisplayID();
}

// ---------------------------------------------------------------------------
// SckScreenCaptureBackend implementation
// ---------------------------------------------------------------------------
namespace sc {

SckScreenCaptureBackend::SckScreenCaptureBackend(int fps, QObject* parent)
    : ScreenCaptureBackend(fps, parent)
{}

SckScreenCaptureBackend::~SckScreenCaptureBackend()
{
    if (m_active.load())
        stopCapture();
}

void SckScreenCaptureBackend::setScreen(QScreen* screen)
{
    m_screen = screen;
}

// WId is NSView* cast to quintptr. Convert to CGWindowID (== uint32_t).
// Must be called on the main thread (safe NSView/NSWindow access).
void SckScreenCaptureBackend::setExcludedWindowIds(const QList<WId>& wids)
{
    m_excludedCGWindowIds.clear();
    for (WId wid : wids) {
        uint32_t cgId = cgWindowIdForNativeHandle(reinterpret_cast<void*>(wid));
        if (cgId)
            m_excludedCGWindowIds.append(cgId);
    }
    qDebug("[SCK] excluding %lld window(s) from capture", (long long)m_excludedCGWindowIds.size());
}

void SckScreenCaptureBackend::startCapture()
{
    if (@available(macOS 12.3, *)) {
        m_active.store(true);
        m_running = true;

        CGDirectDisplayID displayId = displayIdForQScreen(m_screen);
        QList<uint32_t>   excluded  = m_excludedCGWindowIds;
        int               fps       = m_fps;

        [SCShareableContent
            getShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* err) {

            if (!m_active.load()) return;   // stopCapture() called before we got here

            if (err || !content) {
                QString msg = err
                    ? QString::fromNSString(err.localizedDescription)
                    : QStringLiteral("SCShareableContent returned no content");
                QMetaObject::invokeMethod(this, [this, msg]() {
                    emit errorOccurred(msg);
                }, Qt::QueuedConnection);
                return;
            }

            // ----------------------------------------------------------------
            // Find the SCDisplay matching our CGDirectDisplayID.
            // ----------------------------------------------------------------
            SCDisplay* display = nil;
            for (SCDisplay* d in content.displays) {
                if (d.displayID == displayId) { display = d; break; }
            }
            if (!display) {
                // Fallback: use the first display.
                display = content.displays.firstObject;
                qWarning("[SCK] target display not found in SCShareableContent, using first");
            }
            if (!display) {
                QMetaObject::invokeMethod(this, [this]() {
                    emit errorOccurred(QStringLiteral("No display available for capture"));
                }, Qt::QueuedConnection);
                return;
            }

            // ----------------------------------------------------------------
            // Build the list of SCWindow objects to exclude.
            // ----------------------------------------------------------------
            NSMutableArray<SCWindow*>* excludedWindows = [NSMutableArray array];
            for (SCWindow* w in content.windows) {
                if (excluded.contains((uint32_t)w.windowID))
                    [excludedWindows addObject:w];
            }
            qDebug("[SCK] matched %lu/%lld window(s) to exclude in SCK window list",
                   (unsigned long)excludedWindows.count, (long long)excluded.size());

            // ----------------------------------------------------------------
            // Create the content filter.
            // ----------------------------------------------------------------
            SCContentFilter* filter =
                [[SCContentFilter alloc] initWithDisplay:display
                                        excludingWindows:excludedWindows];

            // ----------------------------------------------------------------
            // Stream configuration — full physical resolution, BGRA pixels.
            // ----------------------------------------------------------------
            // CGDisplayPixelsWide/High returns the logical mode size on HiDPI
            // displays — the same number QScreen::geometry() reports in points.
            // CGDisplayModeGetPixelWidth/Height always returns the true physical
            // backing pixel count (e.g. 3024 instead of 1512 on a 2x Retina Mac).
            // Using the physical size here ensures cropToRegion's scale factor
            // is >1 on Retina displays, producing a full-DPI crop.
            CGDisplayModeRef currentMode = CGDisplayCopyDisplayMode(display.displayID);
            size_t physW = currentMode ? CGDisplayModeGetPixelWidth(currentMode)
                                       : (size_t)CGDisplayPixelsWide(display.displayID);
            size_t physH = currentMode ? CGDisplayModeGetPixelHeight(currentMode)
                                       : (size_t)CGDisplayPixelsHigh(display.displayID);
            if (currentMode) CGDisplayModeRelease(currentMode);

            qDebug("[SCK] display physical pixels: %zux%zu", physW, physH);

            SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
            config.pixelFormat = kCVPixelFormatType_32BGRA;
            config.width       = physW;
            config.height      = physH;
            config.showsCursor = YES;
            // minimumFrameInterval: let SCK pace delivery at the target fps.
            config.minimumFrameInterval = CMTimeMake(1, fps);

            // ----------------------------------------------------------------
            // Create the stream + delegate.
            // ----------------------------------------------------------------
            QMutexLocker lock(&m_stateMutex);
            if (!m_active.load()) return;    // raced with stopCapture()

            auto* state    = new SckState();
            state->delegate = [[SckDelegate alloc] initWithBackend:this];
            state->stream   = [[SCStream alloc] initWithFilter:filter
                                               configuration:config
                                                    delegate:state->delegate];
            m_state = state;
            lock.unlock();

            NSError* addErr = nil;
            [state->stream addStreamOutput:state->delegate
                                      type:SCStreamOutputTypeScreen
                        sampleHandlerQueue:nil
                                     error:&addErr];
            if (addErr) {
                qWarning("[SCK] addStreamOutput error: %s",
                         addErr.localizedDescription.UTF8String);
            }

            [state->stream startCaptureWithCompletionHandler:^(NSError* startErr) {
                if (startErr) {
                    QString msg = QString::fromNSString(startErr.localizedDescription);
                    qWarning("[SCK] startCapture error: %s", qPrintable(msg));
                    QMetaObject::invokeMethod(this, [this, msg]() {
                        emit errorOccurred(msg);
                    }, Qt::QueuedConnection);
                } else {
                    qDebug("[SCK] capture started: %lux%lu @ %d fps, %llu window(s) excluded",
                           (unsigned long)config.width, (unsigned long)config.height,
                           fps, (unsigned long long)excludedWindows.count);
                }
            }];
        }];
    } else {
        // SCK streaming requires macOS 12.3+.  Should not reach here because
        // the factory in ScreenCaptureWorker checks availability.
        emit errorOccurred(QStringLiteral("ScreenCaptureKit requires macOS 12.3 or later"));
    }
}

void SckScreenCaptureBackend::stopCapture()
{
    m_running = false;
    m_active.store(false);

    QMutexLocker lock(&m_stateMutex);
    SckState* state = m_state;
    m_state = nullptr;
    lock.unlock();

    if (!state) return;

    // Invalidate the delegate first so no new invokeMethod calls are posted.
    [state->delegate invalidate];

    // Stop the stream synchronously (wait up to 3 s for the completion handler).
    if (@available(macOS 12.3, *)) {
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        [state->stream stopCaptureWithCompletionHandler:^(NSError* err) {
            if (err)
                qDebug("[SCK] stopCapture: %s", err.localizedDescription.UTF8String);
            dispatch_semaphore_signal(sem);
        }];
        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
    }

    // Release the Obj-C objects (ARC handles the actual dealloc).
    delete state;

    qDebug("[SCK] capture stopped");
}

} // namespace sc
