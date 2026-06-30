#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QDir>
#include "core/bus_protocol.h"
#include "gui/interfacewindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    
    // Load QSS theme
    QString qssPath = QCoreApplication::applicationDirPath() + "/assets/theme.qss";
    if (!QFile::exists(qssPath)) {
        qssPath = QCoreApplication::applicationDirPath() + "/../assets/theme.qss";
    }
    if (!QFile::exists(qssPath)) {
        qssPath = "./assets/theme.qss";
    }
    QFile file(qssPath);
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = QLatin1String(file.readAll());
        a.setStyleSheet(styleSheet);
        qDebug() << "Loaded Stylesheet from:" << qssPath;
    } else {
        qDebug() << "Warning: Could not open stylesheet:" << qssPath;
    }

    BusProtocol protocol;
    qDebug() << "AetherBus version:" << protocol.getProtocolVersion();

    InterfaceWindow w;
    w.show();

    return a.exec();
}

