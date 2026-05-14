#include "mainmenu.hpp"

#include "actions.hpp"

#include <QMenu>
#include <QMenuBar>

namespace sc {

MainMenu::MainMenu(Actions* actions, QObject* parent)
    : QObject(parent)
{
    m_bar = new QMenuBar();
    m_bar->setNativeMenuBar(true);
    buildMenu(actions);
}

MainMenu::~MainMenu()
{
    delete m_bar;
}

void MainMenu::buildMenu(Actions* actions)
{
    auto* recording = m_bar->addMenu(QStringLiteral("Recording"));
    recording->addAction(actions->record);
    recording->addAction(actions->pauseResume);
    recording->addAction(actions->stop);

    auto* format = m_bar->addMenu(QStringLiteral("Format"));
    format->addAction(actions->formatGif);
    format->addAction(actions->formatMp4);

    auto* view = m_bar->addMenu(QStringLiteral("View"));
    view->addAction(actions->openPreview);
    view->addAction(actions->showHide);
    view->addAction(actions->snapAspect);
    view->addAction(actions->followMouse);

    auto* app = m_bar->addMenu(QStringLiteral("App"));
    app->addAction(actions->audio);
    app->addAction(actions->hiDpi);
    app->addSeparator();
    app->addAction(actions->preferences);
    app->addSeparator();
    app->addAction(actions->quit);
}

} // namespace sc
