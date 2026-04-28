#include "appcontroller.hpp"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("ScreenCapture");
    app.setOrganizationName("sc");
    app.setQuitOnLastWindowClosed(false); // windows are tools; closing one shouldn't quit

    sc::AppController controller;
    controller.start();

    return app.exec();
}
