#pragma once

#include <QObject>

class QMenuBar;

namespace sc {

class Actions;

// Owns the native application menu bar and inserts shared Actions.
class MainMenu : public QObject {
    Q_OBJECT

public:
    explicit MainMenu(Actions* actions, QObject* parent = nullptr);
    ~MainMenu() override;

private:
    void buildMenu(Actions* actions);

    QMenuBar* m_bar = nullptr;
};

} // namespace sc
