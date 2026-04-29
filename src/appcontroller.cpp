#include "appcontroller.hpp"
#include "recorderworker.hpp"
#include "capture/screencaptureworker.hpp"
#include "capture/framestore.hpp"
#include "encoding/gifencoder.hpp"
#include "ui/capturewindow.hpp"
#include "ui/controlbar.hpp"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>

namespace sc {

AppController::AppController(QObject* parent)
    : QObject(parent)
    , m_frameStore(new FrameStore(this))
{
    loadSettings();

    // Default capture region: 800×450 centered on the primary screen
    QScreen* primary = QGuiApplication::primaryScreen();
    if (primary) {
        m_region.screen = primary;
        QRect available = primary->availableGeometry();
        int x = available.x() + (available.width()  - 800) / 2;
        int y = available.y() + (available.height() - 450) / 2;
        m_region.rect = QRect(x, y, 800, 450);
    }
}

AppController::~AppController()
{
    teardownWorker();
    saveSettings();
}

void AppController::start()
{
    m_captureWindow = new CaptureWindow(this);
    m_controlBar    = new ControlBar(m_captureWindow);

    // Wire control bar buttons → controller slots
    connect(m_controlBar, &ControlBar::startRequested,  this, &AppController::onStartRequested);
    connect(m_controlBar, &ControlBar::stopRequested,   this, &AppController::onStopRequested);
    connect(m_controlBar, &ControlBar::pauseRequested,  this, &AppController::onPauseRequested);
    connect(m_controlBar, &ControlBar::resumeRequested, this, &AppController::onResumeRequested);

    // Wire controller state → windows
    connect(this, &AppController::stateChanged,  m_captureWindow, &CaptureWindow::onStateChanged);
    connect(this, &AppController::stateChanged,  m_controlBar,    &ControlBar::onStateChanged);
    connect(this, &AppController::regionChanged, m_captureWindow, &CaptureWindow::onRegionChanged);
    connect(this, &AppController::regionChanged, m_controlBar,    &ControlBar::onRegionChanged);

    // Wire capture window drag/resize → controller
    connect(m_captureWindow, &CaptureWindow::regionChanged, this, &AppController::onRegionChanged);

    m_captureWindow->show();
    m_controlBar->show();

    // Push initial state
    emit stateChanged(m_state);
    emit regionChanged(m_region);
    m_controlBar->snapToRegion(m_region.rect);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void AppController::onStartRequested()
{
    if (m_state != AppState::Idle)
        return;

    m_frameStore->clear();

    auto* worker = new ScreenCaptureWorker(m_region, m_settings);
    attachWorker(worker);

    // Every kept frame is buffered into FrameStore on the main thread.
    // The lambda captures the worker's current CaptureRegion at emit time
    // so a moving capture window is recorded accurately per-frame.
    connect(worker, &RecorderWorker::frameReady,
            this, [this, worker](const QVideoFrame& frame) {
                m_frameStore->addFrame(frame, worker->captureRegion());
            },
            Qt::QueuedConnection);

    connect(worker, &RecorderWorker::errorOccurred,
            this,   &AppController::onCaptureError,
            Qt::QueuedConnection);

    setState(AppState::Recording);
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);
}

void AppController::onStopRequested()
{
    if (m_state != AppState::Recording && m_state != AppState::Paused)
        return;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    else
        setState(AppState::Idle); // no worker yet — stub path
}

void AppController::onPauseRequested()
{
    if (m_state != AppState::Recording)
        return;
    setState(AppState::Paused);
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "pause", Qt::QueuedConnection);
}

void AppController::onResumeRequested()
{
    if (m_state != AppState::Paused)
        return;
    setState(AppState::Recording);
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "resume", Qt::QueuedConnection);
}

void AppController::onRegionChanged(const QRect& rect)
{
    m_region.rect = rect;
    if (!m_region.screen)
        m_region.screen = QGuiApplication::primaryScreen();
    emit regionChanged(m_region);

    if (m_controlBar)
        m_controlBar->snapToRegion(rect);

    // Keep the worker's region current so it captures the new position live.
    if (m_worker)
        m_worker->setCaptureRegion(m_region);
}

void AppController::onRecordingFinished()
{
    qDebug("Recording finished. Frames buffered: %d", m_frameStore->frameCount());
    setState(AppState::Processing);

    if (m_frameStore->frameCount() == 0) {
        qWarning("No frames captured — skipping GIF export.");
        setState(AppState::Idle);
        return;
    }

    // Build output path: ~/Movies/capture-YYYY-MM-DD-HHMMSS.gif
    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    const QString outputDir = m_settings.outputDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
        : m_settings.outputDir;
    QDir().mkpath(outputDir);
    const QString outputPath =
        outputDir + QDir::separator() +
        QStringLiteral("capture-%1.gif").arg(timestamp);

    GifExportSettings gifSettings;
    gifSettings.outputFps = qMin(10, m_settings.fps);  // cap at 10 fps for GIF
    gifSettings.maxWidth  = 800;
    gifSettings.quality   = m_settings.quality;

    // Tear down any leftover encoder thread from a previous recording.
    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait();
        m_encoderThread->deleteLater();
        m_encoderThread = nullptr;
    }

    m_encoderThread = new QThread(this);
    auto* encoder = new GifEncoder(m_frameStore, gifSettings, m_settings.fps, outputPath);
    encoder->moveToThread(m_encoderThread);

    connect(m_encoderThread, &QThread::started,
            encoder, &GifEncoder::encode);
    connect(encoder, &GifEncoder::progress,
            this, &AppController::onEncodingProgress);
    connect(encoder, &GifEncoder::finished,
            this, &AppController::onEncodingFinished);
    connect(encoder, &GifEncoder::failed,
            this, &AppController::onEncodingFailed);
    // Clean up encoder object when thread finishes.
    connect(m_encoderThread, &QThread::finished,
            encoder, &QObject::deleteLater);

    m_encoderThread->start();
}

void AppController::onProgressUpdated(qint64 elapsedMs)
{
    emit recordingProgress(elapsedMs);
}

void AppController::onCaptureError(const QString& message)
{
    // Runs on the main thread (QueuedConnection from worker).
    // On macOS this is commonly a screen recording permission error.
    QMessageBox::critical(
        m_controlBar,
        QStringLiteral("Screen Capture Error"),
        message + QStringLiteral("\n\nOn macOS, go to System Settings › Privacy › Screen Recording and grant access.")
    );
}

void AppController::onEncodingProgress(float fraction)
{
    Q_UNUSED(fraction)
    // TODO (Milestone 6): surface to progress indicator in control bar
}

void AppController::onEncodingFinished(const QString& filePath)
{
    qDebug() << "GIF saved:" << filePath;
    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait();
        m_encoderThread->deleteLater();
        m_encoderThread = nullptr;
    }
    setState(AppState::Idle);
    // TODO (Milestone 6): open preview
}

void AppController::onEncodingFailed(const QString& reason)
{
    qWarning() << "GIF encoding failed:" << reason;
    if (m_encoderThread) {
        m_encoderThread->quit();
        m_encoderThread->wait();
        m_encoderThread->deleteLater();
        m_encoderThread = nullptr;
    }
    QMessageBox::critical(
        m_controlBar,
        QStringLiteral("Encoding Error"),
        reason
    );
    setState(AppState::Idle);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void AppController::attachWorker(RecorderWorker* worker)
{
    teardownWorker();

    m_worker       = worker;
    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);

    // Worker → controller
    connect(m_worker, &RecorderWorker::recordingFinished,
            this,     &AppController::onRecordingFinished,
            Qt::QueuedConnection);
    connect(m_worker, &RecorderWorker::progressUpdated,
            this,     &AppController::onProgressUpdated,
            Qt::QueuedConnection);

    // Clean up the thread when the worker is done.
    // Also null our pointers immediately so they're never dangling.
    connect(m_workerThread, &QThread::finished, this, [this]() {
        m_worker       = nullptr;
        m_workerThread = nullptr;
    }, Qt::QueuedConnection);
    connect(m_worker, &RecorderWorker::recordingFinished,
            m_workerThread, &QThread::quit,
            Qt::QueuedConnection);
    connect(m_workerThread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    m_workerThread->start();
}

void AppController::teardownWorker()
{
    if (!m_workerThread)
        return;
    m_workerThread->quit();
    m_workerThread->wait();
    // m_worker deleted by deleteLater connection above
    m_worker       = nullptr;
    m_workerThread = nullptr;
}
// ---------------------------------------------------------------------------

void AppController::setState(AppState s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(m_state);
}

void AppController::loadSettings()
{
    QSettings qs("sc", "ScreenCapture");
    m_settings = RecordingSettings::load(qs);

    QRect savedRect = qs.value("captureRect").toRect();
    if (savedRect.isValid())
        m_region.rect = savedRect;
}

void AppController::saveSettings()
{
    QSettings qs("sc", "ScreenCapture");
    m_settings.save(qs);
    qs.setValue("captureRect", m_region.rect);
}

} // namespace sc
