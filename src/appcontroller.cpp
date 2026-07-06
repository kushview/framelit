#include "appcontroller.hpp"
#include "workermanager.hpp"
#include "mousepanner.hpp"
#include "outputpath.hpp"
#include "ui/capturewindow.hpp"
#include "ui/centerhandle.hpp"
#include "ui/closebutton.hpp"
#include "ui/controlbar.hpp"
#include "ui/editwindow.hpp"
#include "ui/mainmenu.hpp"
#include "ui/preferencesdialog.hpp"
#include "ui/uigeometry.hpp"

#include <QCursor>

#ifdef Q_OS_MAC
#  include "platform/macos_window.hpp"
#  include "globalhotkeys.hpp"
#endif

#include <QGuiApplication>
#include "ui/systemtray.hpp"
#include "ui/actions.hpp"
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QScreen>
#include <QThread>
#include <QUrl>
#include <qwindowdefs.h>

namespace sc {

namespace {

bool isIdleLikeState(AppState state)
{
    return state == AppState::Idle || state == AppState::Preview;
}

bool hasPreviewMediaInDir(const QString& outputDir)
{
    if (outputDir.isEmpty())
        return false;

    const QDir dir(outputDir);
    if (!dir.exists())
        return false;

    const QStringList filters = {
        QStringLiteral("*.gif"),
        QStringLiteral("*.mp4"),
        QStringLiteral("*.png"),
        QStringLiteral("*.webm")
    };
    return !dir.entryList(filters, QDir::Files | QDir::Readable, QDir::Time).isEmpty();
}

} // namespace

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
    // Delete the WorkerManager first so its thread is quit()+wait()'d before
    // the rest of the app tears down.
    delete m_workerManager;
    m_workerManager = nullptr;
    delete m_editWindow;
    m_editWindow = nullptr;
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
    m_closeButton   = new CloseButton();
    m_controlBar    = new ControlBar(m_captureWindow);

    // Recording pipeline (worker + strategy + threads). It reports lifecycle
    // back via signals; AppController maps them to state transitions / UI.
    m_workerManager = new WorkerManager(this);
    connect(m_workerManager, &WorkerManager::progress,
            this, &AppController::onProgressUpdated);
    connect(m_workerManager, &WorkerManager::processingStarted,
            this, [this]() { setState(AppState::Processing); });
    connect(m_workerManager, &WorkerManager::encodingProgress,
            this, &AppController::onEncodingProgress);
    connect(m_workerManager, &WorkerManager::encodingFinished,
            this, &AppController::onEncodingFinished);
    connect(m_workerManager, &WorkerManager::encodingFailed,
            this, &AppController::onEncodingFailed);
    connect(m_workerManager, &WorkerManager::captureError,
            this, &AppController::onCaptureError);

    m_actions = new Actions(this);
    connect(m_actions, &Actions::recordRequested,             this, &AppController::onStartRequested);
    connect(m_actions, &Actions::pauseResumeRequested, this, [this]() {
        if (m_state == AppState::Recording) onPauseRequested();
        else if (m_state == AppState::Paused) onResumeRequested();
    });
    connect(m_actions, &Actions::stopRequested,               this, &AppController::onStopRequested);
    connect(m_actions, &Actions::toggleUiRequested,           this, &AppController::toggleUiVisible);
    connect(m_actions, &Actions::formatChangeRequested,       this, &AppController::onFormatChangeRequested);
    connect(m_actions, &Actions::audioChangeRequested,        this, &AppController::onAudioChangeRequested);
    connect(m_actions, &Actions::hiDpiChangeRequested,        this, &AppController::onHiDpiChangeRequested);
    connect(m_actions, &Actions::followMouseChangeRequested,  this, &AppController::onFollowMouseChangeRequested);
    connect(m_actions, &Actions::snapAspectRequested,         this, &AppController::onSnapAspectRequested);
    connect(m_actions, &Actions::openPreviewRequested,        this, &AppController::onOpenPreviewRequested);
    connect(m_actions, &Actions::openOutputDirRequested,      this, &AppController::onOpenOutputDirRequested);
    connect(m_actions, &Actions::preferencesRequested,        this, &AppController::onPreferencesRequested);
    connect(m_actions, &Actions::quitRequested,               []() { QApplication::quit(); });

    m_editWindow    = new EditWindow(m_actions);
    m_editWindow->setOutputDir(m_settings.outputDir);
    m_editWindow->setAudioOutputDevice(m_settings.audioOutputDeviceId);
    connect(m_editWindow, &EditWindow::closed, this, &AppController::onPreviewClosed);

    m_mainMenu = new MainMenu(m_actions, this);

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
    connect(m_controlBar, &ControlBar::captureRectChangeRequested, this, &AppController::onRegionChanged);
    connect(m_controlBar, &ControlBar::previewToggleRequested,      this, &AppController::onPreviewToggleRequested);
    connect(m_controlBar, &ControlBar::preferencesRequested,       this, &AppController::onPreferencesRequested);

    applySettingsToUI();

    // Wire controller state → windows
    connect(this, &AppController::stateChanged,  m_captureWindow, &CaptureWindow::onStateChanged);
    connect(this, &AppController::stateChanged,  m_controlBar,    &ControlBar::onStateChanged);
    connect(this, &AppController::stateChanged,  this,            [this](AppState) {
        syncCenterHandleVisibility();
    });
    connect(this, &AppController::regionChanged, m_captureWindow, &CaptureWindow::onRegionChanged);
    connect(this, &AppController::regionChanged, m_centerHandle,  &CenterHandle::onRegionChanged);
    connect(this, &AppController::regionChanged, m_closeButton,   &CloseButton::onRegionChanged);
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

    // Close button hides the UI
    connect(m_closeButton, &CloseButton::closeRequested, this, [this]() {
        setUiVisible(false);
    });

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
        m_tray = new SystemTray(m_actions, this);
        m_tray->show();
    }
    syncActions();

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

    // Keep control bar display in sync with canonical settings, including
    // format changes originating from shared menu actions.
    m_controlBar->setFormat(m_settings.format);
    m_controlBar->setCaptureAudio(m_settings.captureAudio);
    m_controlBar->setPreviewVisible(m_previewVisible);

    m_actions->sync(m_state, m_settings,
                    hasPreviewMediaInDir(m_settings.outputDir),
                    m_followMouse,
                    m_captureWindow->isVisible() && m_controlBar->isVisible());
}

void AppController::setUiVisible(bool visible)
{
    if (!m_captureWindow || !m_centerHandle || !m_closeButton || !m_controlBar)
        return;

    if (visible) {
        m_captureWindow->show();
        m_centerHandle->show();
        m_closeButton->show();
        m_controlBar->show();
        m_controlBar->snapToRegion(m_region.rect);
        m_captureWindow->raise();
        m_centerHandle->raise();
        m_closeButton->raise();
        m_controlBar->raise();
    } else {
        m_controlBar->hide();
        m_centerHandle->hide();
        m_closeButton->hide();
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
    if (!isIdleLikeState(m_state))
        return;

    if (m_editWindow && m_editWindow->isVisible())
        m_editWindow->hide();
    m_previewVisible = false;

    // Always exclude our own windows from the SCK content filter so they never
    // appear in the captured frames. Demo mode only controls NSWindowSharingType
    // (visibility to *external* recorders) — it is orthogonal to this list.
    m_workerManager->start(m_region, m_settings, {
        m_captureWindow ? m_captureWindow->winId() : WId{0},
        m_centerHandle  ? m_centerHandle->winId()  : WId{0},
        m_controlBar    ? m_controlBar->winId()    : WId{0},
    });

    setState(AppState::Recording);
}

void AppController::onStopRequested()
{
    if (m_state != AppState::Recording && m_state != AppState::Paused)
        return;
    if (m_workerManager->isActive())
        m_workerManager->stop();
    else
        setState(AppState::Idle); // no worker yet — stub path
}

void AppController::onPauseRequested()
{
    if (m_state != AppState::Recording)
        return;
    setState(AppState::Paused);
    m_workerManager->pause();
}

void AppController::onResumeRequested()
{
    if (m_state != AppState::Paused)
        return;
    setState(AppState::Recording);
    m_workerManager->resume();
}

void AppController::onRegionChanged(const QRect& rect)
{
    m_region.rect = rect;

    // Snap onto the screen the region now sits on, and clamp so it can never be
    // dragged (partly) off-screen — an off-screen region silently breaks the
    // capture crop. The re-emitted regionChanged pushes the clamped rect back
    // to the CaptureWindow.
    QScreen* screen = QGuiApplication::screenAt(rect.center());
    if (!screen)
        screen = m_region.screen ? m_region.screen
                                 : QGuiApplication::primaryScreen();
    m_region.screen = screen;
    if (screen)
        m_region = m_region.clampedTo(screen->geometry());

    // If the user dragged the window (not a hotkey resize), clear the latched
    // aspect so the next hotkey press re-latches from the new shape.
    if (!m_applyingResize)
        m_resizeAspect = 0.0;

    emit regionChanged(m_region);

    if (m_controlBar)
        m_controlBar->snapToRegion(m_region.rect);

    // Keep the worker's region current so it captures the new position live.
    m_workerManager->setCaptureRegion(m_region);
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
    m_lastOutputPath = filePath;
    if (m_editWindow && m_editWindow->isVisible()) {
        m_editWindow->setOutputDir(m_settings.outputDir);
        m_editWindow->selectFile(filePath);
        setState(AppState::Preview);
        return;
    }
    setState(AppState::Idle);
}

void AppController::onEncodingFailed(const QString& reason)
{
    qWarning() << "Encoding failed:" << reason;
    QMessageBox::critical(
        m_controlBar,
        QStringLiteral("Encoding Error"),
        reason
    );
    setState(AppState::Idle);
}

void AppController::onFormatChangeRequested(OutputFormat format)
{
    if (!isIdleLikeState(m_state)) { syncActions(); return; }
    m_settings.format = format;
    saveSettings();
    syncActions();
}

void AppController::onAudioChangeRequested(bool captureAudio)
{
    if (!isIdleLikeState(m_state)) { syncActions(); return; }
    m_settings.captureAudio = captureAudio;
    saveSettings();
    syncActions();
}

void AppController::onHiDpiChangeRequested(bool hiDpi)
{
    if (!isIdleLikeState(m_state)) { syncActions(); return; }
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
    if (!isIdleLikeState(m_state))
        return;
    m_settings.letterbox = letterbox;
    saveSettings();
}

void AppController::onAudioDeviceChangeRequested(const QString& deviceId)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.audioDeviceId = deviceId;
    saveSettings();
}

void AppController::onAudioOutputDeviceChangeRequested(const QString& deviceId)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.audioOutputDeviceId = deviceId;
    if (m_editWindow)
        m_editWindow->setAudioOutputDevice(deviceId);
    saveSettings();
}

void AppController::onOutputDirChangeRequested(const QString& dir)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.outputDir = dir;
    if (m_editWindow)
        m_editWindow->setOutputDir(dir);
    saveSettings();
}

void AppController::onGifOutputSizeChangeRequested(QSize size)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.gifOutputSize = size;
    saveSettings();
}

void AppController::onGifUseFrameSizeChangeRequested(bool on)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.gifUseFrameSize = on;
    saveSettings();
}

void AppController::onOutputSizeChangeRequested(QSize size)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.outputSize = size;
    saveSettings();
}

void AppController::onQualityChangeRequested(QualityPreset quality)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.quality = quality;
    saveSettings();
}

void AppController::onGifQualityChangeRequested(QualityPreset quality)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.gifQuality = quality;
    saveSettings();
}

void AppController::onGrowStepChangeRequested(int step)
{
    if (!isIdleLikeState(m_state))
        return;
    m_settings.growStep = qBound(1, step, 200);
    saveSettings();
}

void AppController::onSnapAspectRequested()
{
    if (!isIdleLikeState(m_state))
        return;
    QRect r = m_region.rect;

    const QSize targetSize = (m_settings.format == OutputFormat::Gif)
        ? m_settings.gifOutputSize
        : m_settings.outputSize;
    const double aspect = (targetSize.height() > 0)
        ? double(targetSize.width()) / targetSize.height()
        : 0.0;
    if (aspect <= 0.0)
        return;

    r.setHeight(heightForAspect(r.width(), aspect, CaptureWindow::kMinDimension));
    onRegionChanged(r);
}

void AppController::onPreferencesRequested()
{
    openPreferencesDialog();
}

void AppController::onOpenOutputDirRequested()
{
    if (m_settings.outputDir.isEmpty())
        return;

    const QDir dir(m_settings.outputDir);
    if (!dir.exists())
        return;

    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void AppController::openPreferencesDialog()
{
    auto* dlg = new PreferencesDialog(m_settings, nullptr);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &PreferencesDialog::gifOutputSizeChangeRequested, this, &AppController::onGifOutputSizeChangeRequested);
    connect(dlg, &PreferencesDialog::gifUseFrameSizeChangeRequested, this, &AppController::onGifUseFrameSizeChangeRequested);
    connect(dlg, &PreferencesDialog::outputDirChangeRequested,  this, &AppController::onOutputDirChangeRequested);
    connect(dlg, &PreferencesDialog::outputSizeChangeRequested, this, &AppController::onOutputSizeChangeRequested);
    connect(dlg, &PreferencesDialog::qualityChangeRequested,    this, &AppController::onQualityChangeRequested);
    connect(dlg, &PreferencesDialog::gifQualityChangeRequested, this, &AppController::onGifQualityChangeRequested);
    connect(dlg, &PreferencesDialog::growStepChangeRequested,   this, &AppController::onGrowStepChangeRequested);
    connect(dlg, &PreferencesDialog::letterboxChangeRequested,  this, &AppController::onLetterboxChangeRequested);
    connect(dlg, &PreferencesDialog::demoModeChangeRequested,   this, &AppController::onDemoModeChangeRequested);

    connect(dlg, &PreferencesDialog::audioInputDeviceChangeRequested, this, &AppController::onAudioDeviceChangeRequested);
    connect(dlg, &PreferencesDialog::audioOutputDeviceChangeRequested, this, &AppController::onAudioOutputDeviceChangeRequested);

    // Hide capture UI while the dialog is open; restore only what was visible.
    const bool captureWasVisible = m_captureWindow && m_captureWindow->isVisible();
    const bool barWasVisible     = m_controlBar    && m_controlBar->isVisible();
    const bool handleWasVisible  = m_centerHandle  && m_centerHandle->isVisible();
    const bool closeButtonWasVisible = m_closeButton && m_closeButton->isVisible();

    if (m_captureWindow) m_captureWindow->hide();
    if (m_controlBar)    m_controlBar->hide();
    if (m_centerHandle)  m_centerHandle->hide();
    if (m_closeButton)   m_closeButton->hide();

    dlg->exec();

    if (captureWasVisible) m_captureWindow->show();
    if (barWasVisible)     m_controlBar->show();
    if (handleWasVisible)  m_centerHandle->show();
    if (closeButtonWasVisible) m_closeButton->show();
    
    syncCenterHandleVisibility();
    syncActions();
}

void AppController::onGrowRequested()   { applyResizeDelta(+m_settings.growStep); }
void AppController::onShrinkRequested() { applyResizeDelta(-m_settings.growStep); }

void AppController::onScreenshotRequested()
{
    if (!m_region.screen || m_region.rect.isEmpty())
        return;

    // Hide all overlay windows so none of them appear in the grab.
    m_centerHandle->hide();
    m_closeButton->hide();
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
        m_closeButton->show();
        m_controlBar->show();
        m_controlBar->snapToRegion(region.rect);
        syncCenterHandleVisibility();
        syncActions();

        if (px.isNull())
            return;

        const QString path = makeCaptureOutputPath(
            m_settings.outputDir,
            QStringLiteral("png"));
        if (!px.save(path, "PNG"))
            return;

        m_lastOutputPath = path;
        if (m_editWindow && m_editWindow->isVisible()) {
            m_editWindow->setOutputDir(m_settings.outputDir);
            m_editWindow->selectFile(path);
        }
        syncActions();
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
    const int newH = heightForAspect(newW, m_resizeAspect, CaptureWindow::kMinDimension);

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
    if (!m_centerHandle || !m_closeButton || !m_captureWindow || !m_controlBar)
        return;

    const bool uiVisible = m_captureWindow->isVisible() && m_controlBar->isVisible();
    const bool showHandle = uiVisible;
    m_centerHandle->setVisible(showHandle);
    m_closeButton->setVisible(showHandle);
    if (showHandle) {
        m_centerHandle->raise();
        m_closeButton->raise();
    }
}

void AppController::centerCaptureRegionAroundCursor()
{
    const QPoint cursor = QCursor::pos();
    QScreen* screen = QGuiApplication::screenAt(cursor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    const QRect bounds = screen->availableGeometry();
    const QSize size = m_region.rect.size().isEmpty()
        ? QSize(800, 450)
        : m_region.rect.size();

    const QRect centered(
        cursor.x() - size.width() / 2,
        cursor.y() - size.height() / 2,
        size.width(),
        size.height());

    m_region = CaptureRegion{screen, centered}.clampedTo(bounds);
    emit regionChanged(m_region);
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
    else if (m_state == AppState::Idle || m_state == AppState::Preview) {
        const bool uiVisible = m_captureWindow && m_captureWindow->isVisible();
        if (!uiVisible)
            centerCaptureRegionAroundCursor();
        setUiVisible(true);
        onStartRequested();
    }
}

void AppController::onPreviewClosed()
{
    m_previewVisible = false;

    if (m_controlBar)
        m_controlBar->setPreviewVisible(false);

    if (m_captureWindow)
        m_captureWindow->show();
    if (m_controlBar) {
        m_controlBar->show();
        m_controlBar->snapToRegion(m_region.rect);
        m_controlBar->raise();
    }
    syncCenterHandleVisibility();

    if (m_state == AppState::Preview)
        setState(AppState::Idle);
    else
        syncActions();
}

void AppController::onOpenPreviewRequested()
{
    onPreviewToggleRequested(true);
}

void AppController::onPreviewToggleRequested(bool show)
{
    if (!isIdleLikeState(m_state) || !m_editWindow)
        return;

    if (show) {
        m_previewVisible = true;
        m_editWindow->setOutputDir(m_settings.outputDir);
        m_editWindow->show();
        m_editWindow->raise();
        m_editWindow->activateWindow();

        if (!m_lastOutputPath.isEmpty())
            m_editWindow->selectFile(m_lastOutputPath);

        if (m_captureWindow)
            m_captureWindow->hide();
        if (m_controlBar)
            m_controlBar->hide();
        syncCenterHandleVisibility();
        setState(AppState::Preview);
        return;
    }

    m_previewVisible = false;

    if (m_controlBar)
        m_controlBar->setPreviewVisible(false);

    m_editWindow->hide();
    if (m_captureWindow) {
        m_captureWindow->show();
        m_captureWindow->raise();
    }
    if (m_controlBar) {
        m_controlBar->show();
        m_controlBar->snapToRegion(m_region.rect);
        m_controlBar->raise();
    }
    syncCenterHandleVisibility();
    if (m_state == AppState::Preview)
        setState(AppState::Idle);
    else
        syncActions();
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
    m_workerManager->setCaptureRegion(m_region);
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
    QSettings qs; // uses app-wide identity set in main() (Kushview / Framelit)
    m_settings = RecordingSettings::load(qs);

    // UI exposes GIF/MP4 only. Migrate any legacy WebM setting to MP4 so
    // recording behavior matches what the controls display.
    if (m_settings.format == OutputFormat::WebM)
        m_settings.format = OutputFormat::Mp4;

    QRect savedRect = qs.value("captureRect").toRect();
    if (savedRect.isValid())
        m_region.rect = savedRect;
}

void AppController::saveSettings()
{
    QSettings qs; // uses app-wide identity set in main() (Kushview / Framelit)
    m_settings.save(qs);
    qs.setValue("captureRect", m_region.rect);
}

} // namespace sc
