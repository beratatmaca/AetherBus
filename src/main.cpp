// AetherBus entrypoint: load the theme stylesheet and show the main
// interception window.
#include "core/signal_cleanup.h"
#include "gui/mainwindow.h"

#include <QApplication>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AetherBus"));
    QCoreApplication::setOrganizationName(QStringLiteral("AetherBus Project"));

    // Release proxy descriptors and unlink slave-PTY symlinks even on a sudden
    // exit (SIGINT/SIGTERM/crash) so devices aren't left locked.
    aether::installSignalHandlers();

    aether::MainWindow window;
    window.show();
    return QApplication::exec();
}
