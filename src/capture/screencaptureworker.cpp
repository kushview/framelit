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
    // Use QueuedConnection so the error handler never runs while setActive()
    // is still on the call stack. A direct/auto connection would let
    // onCaptureError → stop() → delete m_capture fire while we're inside
    // m_capture's own signal emission → use-after-free → SIGSEGV.
    connect(m_capture, &QScreenCapture::errorOccurred,
            this, [this](QScreenCapture::Error err, const QString& msg) {
                onCaptureError(static_cast<int>(err), msg);
            }, Qt::QueuedConnection);

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

    // Emit the QVideoFrame directly — it is ref-counted so this is a
    // pointer bump, not a copy. FrameStore (or the encoder) will call
    // toImage() only on the frames it actually needs.
    //
    // We do attach the crop metadata so FrameStore / encoder knows the
    // capture region at the time this frame was taken, for accurate crop.
    // Note: QVideoFrame does not have crop metadata natively; we store it
    // in a subframe rect. For now, crop at emit time if needed.
    //
    // Map CaptureRegion (logical pixels, global screen coords) to physical
    // pixels relative to the captured screen's origin.
    //
    // We emit the full frame here and let FrameStore/encoder do the crop at
    // decode time so that a moving capture window mid-recording still uses
    // the correct rect per frame (stored alongside in FrameStore).
    emit frameReady(videoFrame);
}

void ScreenCaptureWorker::onCaptureError(int /*error*/, const QString& message)
{
    // On macOS, error code 1 = "screen recording permission not granted".
    // Surface the message to AppController for display.
    emit errorOccurred(message);
    stop();
}

} // namespace sc
