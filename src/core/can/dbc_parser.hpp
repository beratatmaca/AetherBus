#pragma once

#include <QString>
#include <QByteArray>
#include <QMap>
#include <QVector>

namespace aether {

/** @brief One signal definition (SG_ line) within a DBC message. */
struct DbcSignal {
    QString name;
    int startBit = 0;
    int bitLength = 0;
    bool isBigEndian = false;  ///< true = Motorola, false = Intel
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;
    double minVal = 0.0;
    double maxVal = 0.0;
    QString unit;
};

/** @brief One message definition (BO_ line) and the signals it carries. */
struct DbcMessage {
    quint32 id = 0;
    QString name;
    int dlc = 8;
    QVector<DbcSignal> signalList;
};

/** @brief In-memory DBC database mapping CAN message ids to their definitions. */
class DbcDatabase {
public:
    DbcDatabase() = default;
    ~DbcDatabase() = default;

    void clear() { m_messages.clear(); }
    void addMessage(const DbcMessage &msg) { m_messages[msg.id] = msg; }
    [[nodiscard]] const QMap<quint32, DbcMessage>& messages() const { return m_messages; }
    [[nodiscard]] bool contains(quint32 id) const { return m_messages.contains(id); }
    [[nodiscard]] DbcMessage getMessage(quint32 id) const { return m_messages.value(id); }

    /** @brief Parse a DBC file from its content string. */
    bool parse(const QString &content);

    /** @brief Decode a signal from raw payload bytes. */
    static double decodeSignal(const QByteArray &payload, const DbcSignal &sig);

private:
    QMap<quint32, DbcMessage> m_messages;
};

} // namespace aether
