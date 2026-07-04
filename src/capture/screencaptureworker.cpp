#include "screencaptureworker.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QScreen>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_MACOS
#  include "../platform/sckbackend.hpp"
#else
#  include "../platform/qtscreenbackend.hpp"
#endif

namespace sc {

ScreenCaptureWorker::ScreenCaptureWorker(const CaptureRegion& region,
                                         const RecordingSettings& settings,
                                         const QList<WId>& excludedWindowIds,
                                         QObject* parent)
    : RecorderWorker(region, settings, parent)
    , m_frameIntervalMs(1000 / qMax(1, settings.fps))
{
    // Must be constructed on the main thread: setExcludedWindowIds below reaches
    // through NSView → NSWindow on macOS, which is main-thread-only. AppController
    // moveToThread()s us afterwards.
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    // Create the platform backend as a child so it moves to the worker thread
    // automatically when AppController calls moveToThread on this object.
#ifdef Q_OS_MACOS
    m_backend = new SckScreenCaptureBackend(settings.fps, this);
#else
    m_backend = new QtScreenCaptureBackend(settings.fps, this);
#endif

    // setExcludedWindowIds must be called here (main thread) on macOS so
    // that NSView → NSWindow → windowNumber access is safe.
    m_backend->setExcludedWindowIds(excludedWindowIds);
}

ScreenCaptureWorker::~ScreenCaptureWorker() = default;

// ---------------------------------------------------------------------------
// Slots — invoked on the worker thread
// ---------------------------------------------------------------------------

void ScreenCaptureWorker::start()
{
    // Invoked via queued connection after moveToThread — must run on the worker
    // thread, never the main thread (the backend drives capture from here).
    Q_ASSERT(QThread::currentThread() == thread());

    if (m_running)
        return;

    QScreen* screen = captureRegion().screen;
    if (!screen) {
        emit errorOccurred("No screen associated with the capture region.");
        emit recordingFinished();
        return;
    }

    m_backend->setScreen(screen);

    connect(m_backend, &ScreenCaptureBackend::frameArrived,
            this,      &ScreenCaptureWorker::onFrameArrived,
            Qt::AutoConnection);

    connect(m_backend, &ScreenCaptureBackend::errorOccurred,
            this, [this](const QString& msg) {
                emit errorOccurred(msg);
                stop();
            },
            Qt::QueuedConnection);

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(1000);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        emit progressUpdated(m_elapsed.elapsed());
    });

    m_running = true;
    m_paused  = false;
    m_framesReceived = 0;
    m_framesKept     = 0;
    m_lastFrameMs    = -1;
    m_elapsed.start();
    m_progressTimer->start();

    qDebug("[SCW] Starting capture: fps=%d frameIntervalMs=%lld",
           m_settings.fps, m_frameIntervalMs);

    m_backend->startCapture();
}

void ScreenCaptureWorker::stop()
{
    Q_ASSERT(QThread::currentThread() == thread()); // worker thread only

    if (!m_running)
        return;

    qDebug("[SCW] stop(): frames received=%d kept=%d elapsed=%lldms",
           m_framesReceived, m_framesKept, m_elapsed.elapsed());

    m_running = false;

    if (m_progressTimer) {
        m_progressTimer->stop();
    }

    // Disconnect before stopCapture() so no frames arrive during teardown.
    disconnect(m_backend, &ScreenCaptureBackend::frameArrived, this, nullptr);

    m_backend->stopCapture();   // blocks until fully stopped

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
// Frame handling — FPS throttle lives here (backend delivers at max rate)
// ---------------------------------------------------------------------------

void ScreenCaptureWorker::onFrameArrived(const QImage& image)
{
    if (!m_running || m_paused)
        return;

    ++m_framesReceived;

    const qint64 now = m_elapsed.elapsed();
    if (m_lastFrameMs >= 0 && (now - m_lastFrameMs) < m_frameIntervalMs)
        return;
    m_lastFrameMs = now;

    ++m_framesKept;

    if (m_framesKept % 30 == 0) {
        qDebug("[SCW] frames received=%d kept=%d elapsed=%lldms (effective ~%.1f fps)",
               m_framesReceived, m_framesKept, now,
               now > 0 ? m_framesKept * 1000.0 / now : 0.0);
    }

    emit frameReady(image, captureRegion());
}

} // namespace sc
