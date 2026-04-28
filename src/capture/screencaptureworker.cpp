#include "screencaptureworker.hpp"

#include <QMediaCaptureSession>
#include <QMessageBox>
#include <QScreen>
#include <QScreenCapture>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

namespace sc {

ScreenCaptureWorker::ScreenCaptureWorker(const CaptureRegion& region,
                                         const RecordingSettings& settings,
                                         QObject* parent)
    : RecorderWorker(region, settings, parent)
    , m_frameIntervalMs(1000 / qMax(1, settings.fps))
{}

ScreenCaptureWorker::~ScreenCaptureWorker() = default;

// ---------------------------------------------------------------------------
// Slots — called on the worker thread
// ---------------------------------------------------------------------------

void ScreenCaptureWorker::start()
{
    if (m_running)
        return;

    QScreen* screen = captureRegion().screen;
    if (!screen) {
        emit errorOccurred("No screen associated with the capture region.");
        emit recordingFinished();
        return;
    }

    // Build the pipeline on this (worker) thread.
    m_capture = new QScreenCapture(this);
    m_capture->setScreen(screen);

    m_session = new QMediaCaptureSession(this);
    m_session->setScreenCapture(m_capture);

    m_sink = new QVideoSink(this);
    m_session->setVideoSink(m_sink);

    // videoFrameChanged arrives from Qt multimedia's internal thread.
    // Qt::AutoConnection queues it to THIS thread's event loop.
    connect(m_sink, &QVideoSink::videoFrameChanged,
            this,   &ScreenCaptureWorker::onFrameReceived,
            Qt::AutoConnection);

    // QScreenCapture::errorOccurred(QScreenCapture::Error, QString) — Qt 6.5+
    // Use a lambda to bridge the typed enum to our untyped int overload so
    // the signal is forward-compatible without depending on the enum header
    // being visible throughout the codebase.
    connect(m_capture, &QScreenCapture::errorOccurred,
            this, [this](QScreenCapture::Error err, const QString& msg) {
                onCaptureError(static_cast<int>(err), msg);
            });

    // Progress timer — emits every second on the worker thread.
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(1000);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        emit progressUpdated(m_elapsed.elapsed());
    });

    m_running = true;
    m_paused  = false;
    m_lastFrameMs = -1;
    m_elapsed.start();
    m_progressTimer->start();
    m_capture->setActive(true);
}

void ScreenCaptureWorker::stop()
{
    if (!m_running)
        return;

    m_running = false;

    if (m_progressTimer) {
        m_progressTimer->stop();
    }

    // Disconnect the sink BEFORE deactivating the pipeline so no
    // frames arrive during teardown.
    if (m_sink)
        disconnect(m_sink, &QVideoSink::videoFrameChanged, this, nullptr);

    if (m_capture)
        m_capture->setActive(false);

    // Explicitly destroy multimedia objects on THIS thread in reverse
    // construction order. If we leave them as children of the worker
    // they will be destroyed during thread cleanup — at which point
    // QMediaCaptureSession's destructor tries to sync with the main
    // thread via a blocking call, causing a deadlock when the main
    // thread is blocked inside QThread::wait().
    delete m_session;  m_session = nullptr;
    delete m_capture;  m_capture = nullptr;
    delete m_sink;     m_sink    = nullptr;

    emit recordingFinished();
}

void ScreenCaptureWorker::pause()
{
    m_paused = true;
}

void ScreenCaptureWorker::resume()
{
    m_paused = false;
}

// ---------------------------------------------------------------------------
// Frame handling
// ---------------------------------------------------------------------------

void ScreenCaptureWorker::onFrameReceived(const QVideoFrame& videoFrame)
{
    if (!m_running || m_paused)
        return;

    // FPS throttle: skip frames that arrive faster than the target rate.
    const qint64 now = m_elapsed.elapsed();
    if (m_lastFrameMs >= 0 && (now - m_lastFrameMs) < m_frameIntervalMs)
        return;
    m_lastFrameMs = now;

    // Convert to QImage (CPU copy, runs on worker thread — not on the UI thread).
    QImage fullFrame = videoFrame.toImage();
    if (fullFrame.isNull())
        return;

    // Map CaptureRegion (logical pixels, global screen coords) to physical
    // pixels relative to the captured screen's origin.
    //
    // Example on a 2× Retina display with a 1440-wide logical screen:
    //   region.rect = QRect(100, 50, 800, 450)  — logical, global
    //   screen.geometry().topLeft() = QPoint(0, 0)
    //   relativeRect = QRect(100, 50, 800, 450)
    //   pixelRect = QRect(200, 100, 1600, 900)  — physical
    const CaptureRegion cr = captureRegion();
    const QRect screenGeom = cr.screen ? cr.screen->geometry() : QRect();
    const qreal dpr = cr.screen ? cr.screen->devicePixelRatio() : 1.0;

    const QRect relativeRect = cr.rect.translated(-screenGeom.topLeft());
    const QRect pixelRect(
        qRound(relativeRect.x()      * dpr),
        qRound(relativeRect.y()      * dpr),
        qRound(relativeRect.width()  * dpr),
        qRound(relativeRect.height() * dpr)
    );

    const QRect clampedPixelRect = pixelRect.intersected(fullFrame.rect());
    if (clampedPixelRect.isEmpty())
        return;

    emit frameReady(fullFrame.copy(clampedPixelRect));
}

void ScreenCaptureWorker::onCaptureError(int /*error*/, const QString& message)
{
    // On macOS, error code 1 = "screen recording permission not granted".
    // Surface the message to AppController for display.
    emit errorOccurred(message);
    stop();
}

} // namespace sc
