#include "core/common/signal_cleanup.hpp"
#include "gui/mainwindow.hpp"
#include "aether/version.h"

#include <QApplication>
#include <QIcon>

#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
    // Answered before QApplication exists so it works with no display server
    // (used by packaging smoke tests and scripting).
    bool enableControl = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("AetherBus %s\n", AETHER_VERSION_STRING);
            return 0;
        }
        if (std::strcmp(argv[i], "--no-control") == 0) {
            enableControl = false;  // disable the localhost scripting/control socket
        }
    }

    Q_INIT_RESOURCE(resources);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AetherBus"));
    QCoreApplication::setOrganizationName(QStringLiteral("AetherBus Project"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AETHER_VERSION_STRING));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/aetherbus/icon.ico")));

    aether::installSignalHandlers();

    aether::MainWindow window(enableControl);
    window.show();
    return QApplication::exec();
}
