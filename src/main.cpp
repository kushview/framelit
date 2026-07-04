#include "appcontroller.hpp"

#include <QApplication>
#include <QFile>
#include <QStyle>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Framelit");
    app.setOrganizationName("Kushview");
    app.setOrganizationDomain("kushview.net");
    app.setQuitOnLastWindowClosed(false); // windows are tools; closing one shouldn't quit

    // Set Fusion style for cross-platform consistency
    app.setStyle("Fusion");

    // Load dark theme stylesheet
    QFile styleFile(":/dark.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        app.setStyleSheet(QLatin1String(styleFile.readAll()));
        styleFile.close();
    }

    sc::AppController controller;
    controller.start();

    return app.exec();
}
