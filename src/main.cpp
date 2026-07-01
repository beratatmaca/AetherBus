#include "core/signal_cleanup.h"
#include "gui/mainwindow.h"
#include "aether/version.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char **argv) {
    Q_INIT_RESOURCE(resources);

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AetherBus"));
    QCoreApplication::setOrganizationName(QStringLiteral("AetherBus Project"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AETHER_VERSION_STRING));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/aetherbus/icon.ico")));

    aether::installSignalHandlers();

    aether::MainWindow window;
    window.show();
    return QApplication::exec();
}
