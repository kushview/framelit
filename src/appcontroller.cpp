#include "appcontroller.hpp"
#include "recorderworker.hpp"
#include "capture/screencaptureworker.hpp"
#include "recordingstrategy.hpp"
#include "bufferedstrategy.hpp"
#include "streamingstrategy.hpp"
#include "mousepanner.hpp"
#include "ui/capturewindow.hpp"
#include "ui/controlbar.hpp"

#include <QCursor>

#ifdef Q_OS_MAC
#  include "platform/macos_window.h"
#  include "globalinputmanager.hpp"
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
    // Request permissions serially — macOS shows only one TCC dialog at a time.
    // CGRequestScreenCaptureAccess() is async (returns before the dialog is dismissed),
    // so if we fire both at once the Accessibility request opens System Preferences
    // silently behind the screen recording dialog and is never seen.
    // Strategy: request screen recording first. If it's already granted, request
    // accessibility. If not, stop — the user will see the screen recording dialog
    // now and get the accessibility request on the next launch.
    if (requestScreenRecordingPermission())
        requestAccessibilityPermission();
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
    connect(m_controlBar, &ControlBar::hiDpiChangeRequested,       this, &AppController::onHiDpiChangeRequested);
    connect(m_controlBar, &ControlBar::audioDeviceChangeRequested, this, &AppController::onAudioDeviceChangeRequested);
    connect(m_controlBar, &ControlBar::outputDirChangeRequested,   this, &AppController::onOutputDirChangeRequested);
    connect(m_controlBar, &ControlBar::outputSizeChangeRequested,  this, &AppController::onOutputSizeChangeRequested);
    connect(m_controlBar, &ControlBar::growStepChangeRequested,    this, &AppController::onGrowStepChangeRequested);
    connect(m_controlBar, &ControlBar::followMouseChangeRequested, this, &AppController::onFollowMouseChangeRequested);
    connect(m_controlBar, &ControlBar::snapAspectRequested,        this, &AppController::onSnapAspectRequested);

    // Restore saved settings into the control bar UI.
    m_controlBar->setAudioDeviceId(m_settings.audioDeviceId);
    m_controlBar->setOutputDir(m_settings.outputDir);
    m_controlBar->setOutputSize(m_settings.outputSize);
    m_controlBar->setGrowStep(m_settings.growStep);
    m_controlBar->setFormat(m_settings.format);
    m_controlBar->setHiDpi(m_settings.hiDpi);

    // Wire controller state → windows
    connect(this, &AppController::stateChanged,  m_captureWindow, &CaptureWindow::onStateChanged);
    connect(this, &AppController::stateChanged,  m_controlBar,    &ControlBar::onStateChanged);
    connect(this, &AppController::regionChanged, m_captureWindow, &CaptureWindow::onRegionChanged);
    connect(this, &AppController::regionChanged, m_controlBar,    &ControlBar::onRegionChanged);

    // Wire capture window drag/resize → controller
    connect(m_captureWindow, &CaptureWindow::regionChanged, this, &AppController::onRegionChanged);

    // Pre-position before show() so macOS doesn't lock in the constructor default.
    m_captureWindow->setGeometry(m_region.rect);

#ifdef Q_OS_MAC
    m_hotkeyManager = new GlobalInputManager(this);
    connect(m_hotkeyManager, &GlobalInputManager::growRequested,              this, &AppController::onGrowRequested);
    connect(m_hotkeyManager, &GlobalInputManager::shrinkRequested,            this, &AppController::onShrinkRequested);
    connect(m_hotkeyManager, &GlobalInputManager::followMouseToggleRequested, this, &AppController::onFollowMouseToggleRequested);
#endif

    // Follow-mouse pan timer — runs at 60 Hz during recording when enabled.
    m_followTimer = new QTimer(this);
    m_followTimer->setInterval(16);
    connect(m_followTimer, &QTimer::timeout, this, &AppController::onFollowMouseTick);

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

    // If the user dragged the window (not a hotkey resize), clear the latched
    // aspect so the next hotkey press re-latches from the new shape.
    if (!m_applyingResize)
        m_resizeAspect = 0.0;

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

void AppController::onHiDpiChangeRequested(bool hiDpi)
{
    if (m_state != AppState::Idle)
        return;
    m_settings.hiDpi = hiDpi;
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

void AppController::onOutputSizeChangeRequested(QSize size)
{
    if (m_state != AppState::Idle)
        return;
    m_settings.outputSize = size;
    saveSettings();
}

void AppController::onGrowStepChangeRequested(int step)
{
    if (m_state != AppState::Idle)
        return;
    m_settings.growStep = qBound(1, step, 200);
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

void AppController::onGrowRequested()   { applyResizeDelta(+m_settings.growStep); }
void AppController::onShrinkRequested() { applyResizeDelta(-m_settings.growStep); }

void AppController::applyResizeDelta(int delta)
{
    QRect r = m_region.rect;
    if (r.isEmpty()) return;

    // Latch aspect ratio on first press; reuse it for all subsequent presses
    // in the same sequence so integer rounding doesn't drift the ratio.
    if (m_resizeAspect <= 0.0)
        m_resizeAspect = double(r.width()) / r.height();

    const int newW = qMax(CaptureWindow::kMinDimension, r.width() + delta);
    const int newH = qMax(CaptureWindow::kMinDimension, qRound(newW / m_resizeAspect));

    const QPoint center = r.center();
    r.setWidth(newW);
    r.setHeight(newH);
    r.moveCenter(center);

    m_applyingResize = true;
    onRegionChanged(r);
    m_applyingResize = false;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void AppController::updateFollowTimer()
{
    if (!m_followTimer)
        return;
    const bool active = m_followMouse &&
                        (m_state == AppState::Recording || m_state == AppState::Paused);
    if (active)
        m_followTimer->start();
    else
        m_followTimer->stop();
}

void AppController::onFollowMouseChangeRequested(bool enabled)
{
    m_followMouse = enabled;
    qDebug("[follow-mouse] enabled=%d state=%d", enabled, int(m_state));
    updateFollowTimer();
    qDebug("[follow-mouse] timer active=%d", m_followTimer->isActive());
}

void AppController::onFollowMouseToggleRequested()
{
    onFollowMouseChangeRequested(!m_followMouse);
    if (m_controlBar)
        m_controlBar->setFollowMouse(m_followMouse);
}

void AppController::onFollowMouseTick()
{
    const QPoint cursor = QCursor::pos();
    const QRect  rect   = m_region.rect;

    const QRect screenRect = m_region.screen
        ? m_region.screen->geometry()
        : QGuiApplication::primaryScreen()->geometry();

    if (!screenRect.contains(cursor))
        return;

    const QRect newRect = MousePanner{}.pan(cursor, rect, screenRect);
    qDebug("[follow-mouse] cursor=(%d,%d) rect=(%d,%d %dx%d) newRect=(%d,%d %dx%d)",
           cursor.x(), cursor.y(),
           rect.x(), rect.y(), rect.width(), rect.height(),
           newRect.x(), newRect.y(), newRect.width(), newRect.height());

    if (newRect == rect)
        return;

    m_region.rect = newRect;
    emit regionChanged(m_region);
    if (m_worker)
        m_worker->setCaptureRegion(m_region);
}

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
    updateFollowTimer();
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
