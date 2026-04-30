#include "appcontroller.hpp"
#include "recorderworker.hpp"
#include "capture/screencaptureworker.hpp"
#include "recordingstrategy.hpp"
#include "bufferedstrategy.hpp"
#include "streamingstrategy.hpp"
#include "ui/capturewindow.hpp"
#include "ui/controlbar.hpp"

#ifdef Q_OS_MAC
#  include "platform/macos_window.h"
#endif

#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QThread>
#include <qwindowdefs.h>

namespace sc {

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    loadSettings();

    // If no saved rect, default to 800×450 centered on the primary screen.
    QScreen* primary = QGuiApplication::primaryScreen();
    if (primary) {
        m_region.screen = primary;
        if (!m_region.rect.isValid()) {
            QRect available = primary->availableGeometry();
            int x = available.x() + (available.width()  - 800) / 2;
            int y = available.y() + (available.height() - 450) / 2;
            m_region.rect = QRect(x, y, 800, 450);
        }
    }
}

AppController::~AppController()
{
    teardownWorker();
    saveSettings();
}

void AppController::start()
{
#ifdef Q_OS_MAC
    // Trigger the TCC screen recording consent prompt at startup so the
    // system dialog appears before the user clicks Record, not racing with it.
    requestScreenRecordingPermission();
#endif

    m_captureWindow = new CaptureWindow(this);
    m_controlBar    = new ControlBar(m_captureWindow);

    // Wire control bar buttons → controller slots
    connect(m_controlBar, &ControlBar::startRequested,        this, &AppController::onStartRequested);
    connect(m_controlBar, &ControlBar::stopRequested,           this, &AppController::onStopRequested);
    connect(m_controlBar, &ControlBar::pauseRequested,          this, &AppController::onPauseRequested);
    connect(m_controlBar, &ControlBar::resumeRequested,         this, &AppController::onResumeRequested);
    connect(m_controlBar, &ControlBar::formatChangeRequested,   this, &AppController::onFormatChangeRequested);
    connect(m_controlBar, &ControlBar::audioChangeRequested,       this, &AppController::onAudioChangeRequested);
    connect(m_controlBar, &ControlBar::audioDeviceChangeRequested, this, &AppController::onAudioDeviceChangeRequested);
    connect(m_controlBar, &ControlBar::outputDirChangeRequested,   this, &AppController::onOutputDirChangeRequested);
    connect(m_controlBar, &ControlBar::snapAspectRequested,        this, &AppController::onSnapAspectRequested);

    // Restore saved settings into the control bar UI.
    m_controlBar->setAudioDeviceId(m_settings.audioDeviceId);
    m_controlBar->setOutputDir(m_settings.outputDir);
    m_controlBar->setFormat(m_settings.format);

    // Wire controller state → windows
    connect(this, &AppController::stateChanged,  m_captureWindow, &CaptureWindow::onStateChanged);
    connect(this, &AppController::stateChanged,  m_controlBar,    &ControlBar::onStateChanged);
    connect(this, &AppController::regionChanged, m_captureWindow, &CaptureWindow::onRegionChanged);
    connect(this, &AppController::regionChanged, m_controlBar,    &ControlBar::onRegionChanged);

    // Wire capture window drag/resize → controller
    connect(m_captureWindow, &CaptureWindow::regionChanged, this, &AppController::onRegionChanged);

    // Pre-position before show() so macOS doesn't lock in the constructor default.
    m_captureWindow->setGeometry(m_region.rect);

    m_captureWindow->show();
    m_controlBar->show();

    const QRect targetRect = m_region.rect;
    emit stateChanged(m_state);
    emit regionChanged(m_region);
    m_controlBar->snapToRegion(targetRect);

    // Reapply after the event loop drains show()-related move/resize events,
    // which on macOS can override the pre-show setGeometry call.
    QTimer::singleShot(0, this, [this, targetRect]() {
        m_region.rect = targetRect;
        emit regionChanged(m_region);
        m_controlBar->snapToRegion(targetRect);
    });
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void AppController::onStartRequested()
{
    if (m_state != AppState::Idle)
        return;

    // Create strategy before the worker so it's ready to receive frames.
    if (m_settings.format == OutputFormat::Gif)
        m_strategy = new BufferedStrategy(m_settings, this);
    else
        m_strategy = new StreamingStrategy(m_settings, this);
    connect(m_strategy, &RecordingStrategy::encodingProgress,
            this, &AppController::onEncodingProgress);
    connect(m_strategy, &RecordingStrategy::encodingFinished,
            this, &AppController::onEncodingFinished);
    connect(m_strategy, &RecordingStrategy::encodingFailed,
            this, &AppController::onEncodingFailed);

    const bool demo = m_controlBar && m_controlBar->demoMode();
    auto* worker = new ScreenCaptureWorker(m_region, m_settings, demo ? QList<WId>{} : QList<WId>{
        m_captureWindow ? m_captureWindow->winId() : WId{0},
        m_controlBar    ? m_controlBar->winId()    : WId{0},
    });
    attachWorker(worker);

    // Route captured frames to the strategy.
    connect(worker, &RecorderWorker::frameReady,
            this, [this](const QImage& image, const sc::CaptureRegion& region) {
                if (m_strategy)
                    m_strategy->onFrame(image, region);
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
    setState(AppState::Processing);

    if (m_strategy)
        m_strategy->finish();
    else
        setState(AppState::Idle);
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
    qDebug() << "Output saved:" << filePath;
    // Strategy has finished — release it.
    if (m_strategy) {
        m_strategy->deleteLater();
        m_strategy = nullptr;
    }
    setState(AppState::Idle);
    // TODO (Milestone 6): open preview
}

void AppController::onEncodingFailed(const QString& reason)
{
    qWarning() << "Encoding failed:" << reason;
    if (m_strategy) {
        m_strategy->deleteLater();
        m_strategy = nullptr;
    }
    QMessageBox::critical(
        m_controlBar,
        QStringLiteral("Encoding Error"),
        reason
    );
    setState(AppState::Idle);
}

void AppController::onFormatChangeRequested(OutputFormat format)
{
    if (m_state != AppState::Idle)
        return;
    m_settings.format = format;
    saveSettings();
}

void AppController::onAudioChangeRequested(bool captureAudio)
{
    if (m_state != AppState::Idle)
        return;
    m_settings.captureAudio = captureAudio;
    saveSettings();
}

void AppController::onAudioDeviceChangeRequested(const QString& deviceId)
{
    if (m_state != AppState::Idle)
        return;
    m_settings.audioDeviceId = deviceId;
    saveSettings();
}

void AppController::onOutputDirChangeRequested(const QString& dir)
{
    if (m_state != AppState::Idle)
        return;
    m_settings.outputDir = dir;
    saveSettings();
}

void AppController::onSnapAspectRequested()
{
    if (m_state != AppState::Idle)
        return;
    QRect r = m_region.rect;
    // Landscape → 16:9; portrait/square → 9:16.
    if (r.width() >= r.height())
        r.setHeight(r.width() * 9 / 16);
    else
        r.setWidth(r.height() * 9 / 16);
    onRegionChanged(r);
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
