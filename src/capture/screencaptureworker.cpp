#include "screencaptureworker.hpp"

#include <QDebug>
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
    m_errorReported = false;
    m_lastFrameMs = -1;
    m_framesReceived = 0;
    m_framesKept = 0;
    m_elapsed.start();
    m_progressTimer->start();

    qDebug("[SCW] Starting capture: fps=%d frameIntervalMs=%lld",
           m_settings.fps, m_frameIntervalMs);

    m_capture->setActive(true);
}

void ScreenCaptureWorker::stop()
{
    if (!m_running)
        return;

    qDebug("[SCW] stop(): frames received=%d kept=%d elapsed=%lldms",
           m_framesReceived, m_framesKept, m_elapsed.elapsed());

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

    ++m_framesReceived;

    // FPS throttle: skip frames that arrive faster than the target rate.
    const qint64 now = m_elapsed.elapsed();
    if (m_lastFrameMs >= 0 && (now - m_lastFrameMs) < m_frameIntervalMs)
        return;
    m_lastFrameMs = now;

    ++m_framesKept;

    // Log every 30 kept frames so we can see the effective capture rate.
    if (m_framesKept % 30 == 0) {
        qDebug("[SCW] frames received=%d kept=%d elapsed=%lldms (effective ~%.1f fps)",
               m_framesReceived, m_framesKept, now,
               now > 0 ? m_framesKept * 1000.0 / now : 0.0);
    }

    // Convert to QImage HERE on the worker thread before emitting.
    //
    // On macOS, QVideoFrame wraps a CMSampleBuffer from ScreenCaptureKit.
    // ScreenCaptureKit maintains a small internal buffer pool (~8-16 frames).
    // If we store QVideoFrame in FrameStore, each held frame keeps its
    // CMSampleBuffer reference alive. Once the pool is exhausted, SCK stops
    // delivering new frames entirely — causing the "always N frames" bug.
    //
    // Calling toImage() copies the pixels out and releases the CMSampleBuffer
    // back to the pool immediately, allowing SCK to keep streaming.
    QImage img = videoFrame.toImage();
    if (img.isNull())
        return;

    // Snapshot the current capture region under the mutex so it's consistent
    // with this frame's timestamp (capture window may be moving mid-recording).
    emit frameReady(img, captureRegion());
}

void ScreenCaptureWorker::onCaptureError(int error, const QString& message)
{
    qDebug("[SCW] onCaptureError: code=%d msg=%s (already reported=%s)",
           error, qPrintable(message), m_errorReported ? "yes" : "no");

    // Guard: QueuedConnection can queue multiple error signals before stop()
    // runs. Only report and stop once.
    if (m_errorReported)
        return;
    m_errorReported = true;

    // On macOS, error code 1 = "screen recording permission not granted".
    // Surface the message to AppController for display.
    emit errorOccurred(message);
    stop();
}

} // namespace sc
