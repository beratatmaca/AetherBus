#pragma once

#include <QObject>
#include <QString>

class BusProtocol : public QObject {
    Q_OBJECT
public:
    explicit BusProtocol(QObject *parent = nullptr);
    QString getProtocolVersion() const;
};
