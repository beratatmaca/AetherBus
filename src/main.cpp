// AetherBus entrypoint: load the theme stylesheet and show the main
// interception window.
#include "gui/mainwindow.h"

#include <QApplication>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AetherBus"));
    QCoreApplication::setOrganizationName(QStringLiteral("AetherBus Project"));

    aether::MainWindow window;
    window.show();
    return QApplication::exec();
}
