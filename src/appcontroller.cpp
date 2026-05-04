#include "appcontroller.hpp"
#include "recorderworker.hpp"
#include "capture/screencaptureworker.hpp"
#include "recordingstrategy.hpp"
#include "bufferedstrategy.hpp"
#include "streamingstrategy.hpp"
#include "mousepanner.hpp"
#include "outputpath.hpp"
#include "ui/capturewindow.hpp"
#include "ui/centerhandle.hpp"
#include "ui/controlbar.hpp"

#include <QCursor>

#ifdef Q_OS_MAC
#  include "platform/macos_window.h"
#  include "globalhotkeys.hpp"
#endif

#include <QGuiApplication>
#include "ui/systemtray.hpp"
#include "ui/actions.hpp"
#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QScreen>
#include <QThread>
#include <QUrl>
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
    requestMicrophonePermission();
#endif

    m_captureWindow = new CaptureWindow(this);
    m_centerHandle  = new CenterHandle();
    m_controlBar    = new ControlBar(m_captureWindow);

    // Wire control bar buttons → controller slots
    connect(m_controlBar, &ControlBar::startRequested,        this, &AppController::onStartRequested);
    connect(m_controlBar, &ControlBar::stopRequested,           this, &AppController::onStopRequested);
    connect(m_controlBar, &ControlBar::pauseRequested,          this, &AppController::onPauseRequested);
    connect(m_controlBar, &ControlBar::resumeRequested,         this, &AppController::onResumeRequested);
    connect(m_controlBar, &ControlBar::formatChangeRequested,   this, &AppController::onFormatChangeRequested);
    connect(m_controlBar, &ControlBar::audioChangeRequested,       this, &AppController::onAudioChangeRequested);
    connect(m_controlBar, &ControlBar::hiDpiChangeRequested,       this, &AppController::onHiDpiChangeRequested);
    connect(m_controlBar, &ControlBar::demoModeChangeRequested,    this, &AppController::onDemoModeChangeRequested);
    connect(m_controlBar, &ControlBar::audioDeviceChangeRequested, this, &AppController::onAudioDeviceChangeRequested);
    connect(m_controlBar, &ControlBar::outputDirChangeRequested,   this, &AppController::onOutputDirChangeRequested);
    connect(m_controlBar, &ControlBar::outputSizeChangeRequested,  this, &AppController::onOutputSizeChangeRequested);
    connect(m_controlBar, &ControlBar::growStepChangeRequested,    this, &AppController::onGrowStepChangeRequested);
    connect(m_controlBar, &ControlBar::followMouseChangeRequested, this, &AppController::onFollowMouseChangeRequested);
    connect(m_controlBar, &ControlBar::letterboxChangeRequested,    this, &AppController::onLetterboxChangeRequested);
    connect(m_controlBar, &ControlBar::snapAspectRequested,        this, &AppController::onSnapAspectRequested);

    applySettingsToUI();

    // Wire controller state → windows
    connect(this, &AppController::stateChanged,  m_captureWindow, &CaptureWindow::onStateChanged);
    connect(this, &AppController::stateChanged,  m_controlBar,    &ControlBar::onStateChanged);
    connect(this, &AppController::stateChanged,  this,            [this](AppState) {
        syncCenterHandleVisibility();
    });
    connect(this, &AppController::regionChanged, m_captureWindow, &CaptureWindow::onRegionChanged);
    connect(this, &AppController::regionChanged, m_centerHandle,  &CenterHandle::onRegionChanged);
    connect(this, &AppController::regionChanged, m_controlBar,    &ControlBar::onRegionChanged);

    // Center handle drag moves the whole capture region while the frame is click-through.
    connect(m_centerHandle, &CenterHandle::dragDelta, this, [this](const QPoint& delta) {
        if (delta.isNull())
            return;
        QRect moved = m_region.rect.translated(delta);
        const QRect bounds = m_region.screen
            ? m_region.screen->geometry()
            : QGuiApplication::primaryScreen()->geometry();
        const CaptureRegion clamped = CaptureRegion{m_region.screen, moved}.clampedTo(bounds);
        onRegionChanged(clamped.rect);
    });
    connect(m_centerHandle, &CenterHandle::wheelResizeRequested, this, [this](int direction) {
        if (direction > 0)
            onGrowRequested();
        else if (direction < 0)
            onShrinkRequested();
    });
    connect(m_centerHandle, &CenterHandle::screenshotRequested, this, &AppController::onScreenshotRequested);
    connect(m_centerHandle, &CenterHandle::screenshotRequested, m_captureWindow, &CaptureWindow::flashGreen);

    // Wire capture window drag/resize → controller
    connect(m_captureWindow, &CaptureWindow::regionChanged, this, &AppController::onRegionChanged);

    // Pre-position before show() so macOS doesn't lock in the constructor default.
    m_captureWindow->setGeometry(m_region.rect);

#ifdef Q_OS_MAC
    m_hotkeyManager = new GlobalHotkeys(this);
    connect(m_hotkeyManager, &GlobalHotkeys::growRequested,              this, &AppController::onGrowRequested);
    connect(m_hotkeyManager, &GlobalHotkeys::shrinkRequested,            this, &AppController::onShrinkRequested);
    connect(m_hotkeyManager, &GlobalHotkeys::followMouseToggleRequested, this, &AppController::onFollowMouseToggleRequested);
    connect(m_hotkeyManager, &GlobalHotkeys::recordToggleRequested,      this, &AppController::onRecordToggleRequested);
    connect(m_hotkeyManager, &GlobalHotkeys::showUiRequested,            this, [this]() {
        setUiVisible(true);
    });
#endif

    // Follow-mouse pan timer — runs at 60 Hz during recording when enabled.
    m_followTimer = new QTimer(this);
    m_followTimer->setInterval(16);
    connect(m_followTimer, &QTimer::timeout, this, &AppController::onFollowMouseTick);

    m_captureWindow->show();
    m_centerHandle->show();
    m_controlBar->show();

    // Apply demo mode after each window's showEvent deferred exclude call fires.
    // singleShot(0) posted here runs after the ones posted inside showEvent().
    if (m_settings.demoMode)
        QTimer::singleShot(0, this, [this]() { applyDemoMode(); });

    if (SystemTray::isAvailable()) {
        m_actions = new Actions(this);
        connect(m_actions, &Actions::recordRequested,          this, &AppController::onStartRequested);
        connect(m_actions, &Actions::pauseResumeRequested, this, [this]() {
            if (m_state == AppState::Recording) onPauseRequested();
            else if (m_state == AppState::Paused) onResumeRequested();
        });
        connect(m_actions, &Actions::stopRequested,            this, &AppController::onStopRequested);
        connect(m_actions, &Actions::toggleUiRequested,        this, &AppController::toggleUiVisible);
        connect(m_actions, &Actions::formatChangeRequested,    this, &AppController::onFormatChangeRequested);
        connect(m_actions, &Actions::audioChangeRequested,     this, &AppController::onAudioChangeRequested);
        connect(m_actions, &Actions::hiDpiChangeRequested,     this, &AppController::onHiDpiChangeRequested);
        connect(m_actions, &Actions::followMouseChangeRequested, this, &AppController::onFollowMouseChangeRequested);
        connect(m_actions, &Actions::snapAspectRequested,      this, &AppController::onSnapAspectRequested);
        connect(m_actions, &Actions::quitRequested,            []() { QApplication::quit(); });

        m_tray = new SystemTray(m_actions, this);
        m_tray->show();
        syncActions();
    }

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

void AppController::syncActions()
{
    if (!m_actions || !m_captureWindow || !m_controlBar)
        return;
    m_actions->sync(m_state, m_settings, m_followMouse,
                    m_captureWindow->isVisible() && m_controlBar->isVisible());
}

void AppController::setUiVisible(bool visible)
{
    if (!m_captureWindow || !m_centerHandle || !m_controlBar)
        return;

    if (visible) {
        m_captureWindow->show();
        m_centerHandle->show();
        m_controlBar->show();
        m_controlBar->snapToRegion(m_region.rect);
        m_captureWindow->raise();
        m_centerHandle->raise();
        m_controlBar->raise();
    } else {
        m_controlBar->hide();
        m_centerHandle->hide();
        m_captureWindow->hide();
    }

    syncCenterHandleVisibility();
    syncActions();
}

void AppController::toggleUiVisible()
{
    if (!m_captureWindow || !m_centerHandle || !m_controlBar)
        return;
    const bool visible = m_captureWindow->isVisible() && m_controlBar->isVisible();
    setUiVisible(!visible);
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

    // Always exclude our own windows from the SCK content filter so they never
    // appear in the captured frames. Demo mode only controls NSWindowSharingType
    // (visibility to *external* recorders) — it is orthogonal to this list.
    auto* worker = new ScreenCaptureWorker(m_region, m_settings, {
        m_captureWindow ? m_captureWindow->winId() : WId{0},
        m_centerHandle  ? m_centerHandle->winId()  : WId{0},
        m_controlBar    ? m_controlBar->winId()    : WId{0},
    });
    attachWorker(worker);

    // Route captured frames to the strategy.
    // Store the connection so we can disconnect it before tearing down the
    // strategy — queued frames must not fire against a deleted object.
    m_frameConn = connect(worker, &RecorderWorker::frameReady,
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
    // Disconnect frame delivery first — queued frames must not fire against
    // a strategy that has been marked for deletion.
    disconnect(m_frameConn);
    if (m_strategy) {
        m_strategy->deleteLater();
        m_strategy = nullptr;
    }
    setState(AppState::Idle);
}

void AppController::onEncodingFailed(const QString& reason)
{
    qWarning() << "Encoding failed:" << reason;
    disconnect(m_frameConn);
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
    if (m_state != AppState::Idle) { syncActions(); return; }
    m_settings.format = format;
    saveSettings();
    syncActions();
}

void AppController::onAudioChangeRequested(bool captureAudio)
{
    if (m_state != AppState::Idle) { syncActions(); return; }
    m_settings.captureAudio = captureAudio;
    saveSettings();
    syncActions();
}

void AppController::onHiDpiChangeRequested(bool hiDpi)
{
    if (m_state != AppState::Idle) { syncActions(); return; }
    m_settings.hiDpi = hiDpi;
    saveSettings();
    syncActions();
}

void AppController::onDemoModeChangeRequested(bool on)
{
    m_settings.demoMode = on;
    applyDemoMode();
    saveSettings();
}

void AppController::applyDemoMode()
{
#ifdef Q_OS_MACOS
    const bool exclude = !m_settings.demoMode;
    for (QWidget* w : { static_cast<QWidget*>(m_captureWindow),
                        static_cast<QWidget*>(m_centerHandle),
                        static_cast<QWidget*>(m_controlBar) }) {
        if (w && w->winId())
            setWindowCaptureExcluded(reinterpret_cast<void*>(w->winId()), exclude);
    }
#endif
}

void AppController::onLetterboxChangeRequested(bool letterbox)
{
    if (m_state != AppState::Idle)
        return;
    m_settings.letterbox = letterbox;
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

void AppController::onScreenshotRequested()
{
    if (!m_region.screen || m_region.rect.isEmpty())
        return;

    // Hide all overlay windows so none of them appear in the grab.
    m_centerHandle->hide();
    m_captureWindow->hide();
    m_controlBar->hide();

    const CaptureRegion region = m_region;
    QTimer::singleShot(50, this, [this, region]() {
        const QPixmap px = region.screen->grabWindow(
            0,
            region.rect.x(),
            region.rect.y(),
            region.rect.width(),
            region.rect.height());

        m_captureWindow->show();
        m_centerHandle->show();
        m_controlBar->show();
        m_controlBar->snapToRegion(region.rect);

        if (px.isNull())
            return;

        const QString path = makeCaptureOutputPath(
            m_settings.outputDir,
            QStringLiteral("png"));
        px.save(path, "PNG");
    });
}

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

void AppController::syncCenterHandleVisibility()
{
    if (!m_centerHandle || !m_captureWindow || !m_controlBar)
        return;

    const bool uiVisible = m_captureWindow->isVisible() && m_controlBar->isVisible();
    const bool showHandle = uiVisible;
    m_centerHandle->setVisible(showHandle);
    if (showHandle)
        m_centerHandle->raise();
}

void AppController::onFollowMouseChangeRequested(bool enabled)
{
    m_followMouse = enabled;
    if (m_controlBar)
        m_controlBar->setFollowMouse(enabled);
    updateFollowTimer();
    syncActions();
}

void AppController::onFollowMouseToggleRequested()
{
    onFollowMouseChangeRequested(!m_followMouse);
}

void AppController::onRecordToggleRequested()
{
    if (m_state == AppState::Recording || m_state == AppState::Paused)
        onStopRequested();
    else if (m_state == AppState::Idle || m_state == AppState::Positioning) {
        setUiVisible(true);
        onStartRequested();
    }
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
    syncActions();
}

void AppController::applySettingsToUI()
{
    m_controlBar->setAudioDeviceId(m_settings.audioDeviceId);
    m_controlBar->setOutputDir(m_settings.outputDir);
    m_controlBar->setOutputSize(m_settings.outputSize);
    m_controlBar->setGrowStep(m_settings.growStep);
    m_controlBar->setFormat(m_settings.format);
    m_controlBar->setHiDpi(m_settings.hiDpi);
    m_controlBar->setCaptureAudio(m_settings.captureAudio);
    m_controlBar->setLetterbox(m_settings.letterbox);
    m_controlBar->setDemoMode(m_settings.demoMode);
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
