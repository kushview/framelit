#include "appcontroller.hpp"
#include "recorderworker.hpp"
#include "capture/screencaptureworker.hpp"
#include "ui/capturewindow.hpp"
#include "ui/controlbar.hpp"

#include <QDir>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>

namespace sc {

AppController::AppController(QObject* parent)
    : QObject(parent)
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

    auto* worker = new ScreenCaptureWorker(m_region, m_settings);
    attachWorker(worker);

    // Prototype (Milestone 3): save first frame as PNG, then stop.
    // SingleShotConnection auto-disconnects after the first delivery so
    // subsequent queued frameReady events don't use m_worker after deletion.
    connect(worker, &RecorderWorker::frameReady,
            this,   &AppController::onFirstFrameReceived,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
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
    setState(AppState::Processing);
    // TODO (Milestone 5): hand FrameStore to GifEncoder worker
    setState(AppState::Idle);
}

void AppController::onProgressUpdated(qint64 elapsedMs)
{
    emit recordingProgress(elapsedMs);
}

void AppController::onFirstFrameReceived(const QImage& frame)
{
    // Milestone 3 prototype: save one PNG and stop recording.
    // This connection is disconnected once Milestone 4 (FrameStore) is wired up.
    const QString dir  = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString path = dir + "/screen-capture-test.png";
    if (frame.save(path)) {
        qDebug("Milestone 3: screenshot saved to %s", qPrintable(path));
    } else {
        qWarning("Milestone 3: failed to save screenshot to %s", qPrintable(path));
    }

    // Capture one frame only — stop the worker.
    QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
}

void AppController::onCaptureError(const QString& message)
{
    // Runs on the main thread (QueuedConnection from worker).
    // On macOS this is commonly a screen recording permission error.
    QMessageBox::critical(
        nullptr,
        QStringLiteral("Screen Capture Error"),
        message + QStringLiteral("\n\nOn macOS, go to System Settings › Privacy › Screen Recording and grant access.")
    );
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
