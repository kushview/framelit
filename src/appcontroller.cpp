#include "appcontroller.hpp"
#include "ui/capturewindow.hpp"
#include "ui/controlbar.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>

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
    setState(AppState::Recording);
}

void AppController::onStopRequested()
{
    if (m_state != AppState::Recording && m_state != AppState::Paused)
        return;
    setState(AppState::Processing);
    // TODO: signal encoder worker; for now go straight back to Idle
    setState(AppState::Idle);
}

void AppController::onPauseRequested()
{
    if (m_state != AppState::Recording)
        return;
    setState(AppState::Paused);
}

void AppController::onResumeRequested()
{
    if (m_state != AppState::Paused)
        return;
    setState(AppState::Recording);
}

void AppController::onRegionChanged(const QRect& rect)
{
    m_region.rect = rect;
    if (!m_region.screen)
        m_region.screen = QGuiApplication::primaryScreen();
    emit regionChanged(m_region);

    if (m_controlBar)
        m_controlBar->snapToRegion(rect);
}

// ---------------------------------------------------------------------------
// Helpers
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
    m_settings.fps        = qs.value("fps", 30).toInt();
    m_settings.showCursor = qs.value("showCursor", true).toBool();
    m_settings.showClicks = qs.value("showClicks", true).toBool();
    m_settings.countdown  = qs.value("countdown", false).toBool();
    m_settings.outputDir  = qs.value("outputDir",
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString();

    QRect savedRect = qs.value("captureRect").toRect();
    if (savedRect.isValid())
        m_region.rect = savedRect;
}

void AppController::saveSettings()
{
    QSettings qs("sc", "ScreenCapture");
    qs.setValue("fps",        m_settings.fps);
    qs.setValue("showCursor", m_settings.showCursor);
    qs.setValue("showClicks", m_settings.showClicks);
    qs.setValue("countdown",  m_settings.countdown);
    qs.setValue("outputDir",  m_settings.outputDir);
    qs.setValue("captureRect", m_region.rect);
}

} // namespace sc
