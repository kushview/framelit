#include "systemtray.hpp"
#include "actions.hpp"

#include <QApplication>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

namespace sc {

SystemTray::SystemTray(Actions* actions, QObject* parent)
    : QObject(parent)
{
    m_icon = new QSystemTrayIcon(this);

    QIcon icon = QApplication::windowIcon();
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("camera-video"));
    if (icon.isNull())
        icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);

    m_icon->setIcon(icon);
    m_icon->setToolTip(QStringLiteral("ScreenCapture"));

    buildMenu(actions);
    m_icon->setContextMenu(m_menu);
}

SystemTray::~SystemTray()
{
    if (m_icon)
        m_icon->hide();
}

bool SystemTray::isAvailable()
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void SystemTray::show() { m_icon->show(); }
void SystemTray::hide() { m_icon->hide(); }

void SystemTray::buildMenu(Actions* actions)
{
    m_menu = new QMenu();

    // Recording lifecycle
    m_menu->addAction(actions->record);
    m_menu->addAction(actions->pauseResume);
    m_menu->addAction(actions->stop);
    m_menu->addSeparator();

    // Output format submenu
    auto* formatMenu = m_menu->addMenu(QStringLiteral("Format"));
    formatMenu->addAction(actions->formatGif);
    formatMenu->addAction(actions->formatMp4);

    // Toggle settings
    m_menu->addAction(actions->audio);
    m_menu->addAction(actions->hiDpi);
    m_menu->addAction(actions->followMouse);
    m_menu->addAction(actions->snapAspect);
    m_menu->addSeparator();

    // App actions
    m_menu->addAction(actions->preferences);
    m_menu->addAction(actions->showHide);
    m_menu->addSeparator();
    m_menu->addAction(actions->quit);
}

} // namespace sc
