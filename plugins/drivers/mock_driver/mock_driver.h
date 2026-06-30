#pragma once

#include <QObject>
#include <QtPlugin>
#include <QTimer>
#include "../../../src/core/IBusDriver.h"

class MockDriverPlugin : public QObject, public IBusDriver {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID IBusDriver_IID)
    Q_INTERFACES(IBusDriver)

public:
    explicit MockDriverPlugin(QObject *parent = nullptr);
    ~MockDriverPlugin() override;

    bool initialize(const QString& interfaceName, const QString& configJson) override;
    void terminate() override;
    bool writeFrame(const AetherFrame& frame) override;

private slots:
    void triggerSimulation();

private:
    QTimer* m_timer;
    QString m_interfaceName;
    uint32_t m_counter;
};
