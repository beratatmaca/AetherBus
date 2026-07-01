#pragma once

#include <QString>
#include <QByteArray>
#include <QMap>
#include <QVector>

namespace aether {

struct DbcSignal {
    QString name;
    int startBit = 0;
    int bitLength = 0;
    bool isBigEndian = false; // true = Motorola, false = Intel
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;
    double minVal = 0.0;
    double maxVal = 0.0;
    QString unit;
};

struct DbcMessage {
    quint32 id = 0;
    QString name;
    int dlc = 8;
    QVector<DbcSignal> signalList;
};

class DbcDatabase {
public:
    DbcDatabase() = default;
    ~DbcDatabase() = default;

    void clear() { m_messages.clear(); }
    void addMessage(const DbcMessage &msg) { m_messages[msg.id] = msg; }
    [[nodiscard]] const QMap<quint32, DbcMessage>& messages() const { return m_messages; }
    [[nodiscard]] bool contains(quint32 id) const { return m_messages.contains(id); }
    [[nodiscard]] DbcMessage getMessage(quint32 id) const { return m_messages.value(id); }

    /// Parse a DBC file from its content string
    bool parse(const QString &content);

    /// Decode a signal from raw payload bytes
    static double decodeSignal(const QByteArray &payload, const DbcSignal &sig);

private:
    QMap<quint32, DbcMessage> m_messages;
};

} // namespace aether
