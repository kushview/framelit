#pragma once

#include <QObject>

class QSystemTrayIcon;
class QMenu;

namespace sc {

class Actions;

// Owns the QSystemTrayIcon and its context menu.
// Actions are shared from Actions — the same QAction* objects are inserted
// here and into any future toolbar/menu. Qt propagates state changes to all
// of them automatically; no sync() needed on this class.
class SystemTray : public QObject {
    Q_OBJECT

public:
    // Takes ownership of nothing — Actions is owned by AppController.
    explicit SystemTray(Actions* actions, QObject* parent = nullptr);
    ~SystemTray() override;

    static bool isAvailable();

    void show();
    void hide();

private:
    void buildMenu(Actions* actions);

    QSystemTrayIcon* m_icon = nullptr;
    QMenu*           m_menu = nullptr;
};

} // namespace sc
