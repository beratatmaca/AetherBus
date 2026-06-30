#pragma once
#include <QString>
#include <QObject>
#include <vector>
#include "bus_protocol.h"

class IBusDriver : public QObject {
    Q_OBJECT
public:
    virtual ~IBusDriver() = default;
    
    // Lifecycle Hooks
    virtual bool initialize(const QString& interfaceName, const QString& configJson) = 0;
    virtual void terminate() = 0;
    
    // Direct Egress (Injection)
    virtual bool writeFrame(const AetherFrame& frame) = 0;

signals:
    // Fired asynchronously when raw data passes ingress driver filters
    void frameReceived(const AetherFrame& frame);
    void errorOccurred(const QString& errorMsg);
};

#define IBusDriver_IID "org.aetherbus.IBusDriver"
Q_DECLARE_INTERFACE(IBusDriver, IBusDriver_IID)
