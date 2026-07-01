#include "core/can/dbc_parser.hpp"
#include <QRegularExpression>
#include <QStringList>

namespace aether {

bool DbcDatabase::parse(const QString &content) {
    clear();
    QStringList lines = content.split('\n');

    // Regex for BO_ line: BO_ <id> <name>: <dlc> <transmitter>
    QRegularExpression msgRegex(QStringLiteral(R"(^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+))"));
    // Regex for SG_ line: SG_ <name> [MUX] : <startBit>|<bitLength>@<endianness><sign> (<factor>,<offset>) [<min>|<max>] "<unit>" <receiver>
    QRegularExpression sigRegex(QStringLiteral(R"(^\s*SG_\s+(\w+)\s*(?:\s*[M\w\d]*)?\s*:\s*(\d+)\|(\d+)@(\d+)([\+-])\s*\(([\d\.eE-]+),([\d\.eE-]+)\)\s*\[([\d\.eE-]+)\|([\d\.eE-]+)\]\s*\"([^\"]*)\")"));

    DbcMessage currentMsg;
    bool hasCurrentMsg = false;

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith(QStringLiteral("BO_"))) {
            auto match = msgRegex.match(trimmed);
            if (match.hasMatch()) {
                if (hasCurrentMsg) {
                    addMessage(currentMsg);
                }
                currentMsg.id = match.captured(1).toUInt();
                currentMsg.name = match.captured(2);
                currentMsg.dlc = match.captured(3).toInt();
                currentMsg.signalList.clear();
                hasCurrentMsg = true;
            }
        } else if (trimmed.startsWith(QStringLiteral("SG_")) || line.contains(QStringLiteral("SG_"))) {
            auto match = sigRegex.match(line);
            if (match.hasMatch() && hasCurrentMsg) {
                DbcSignal sig;
                sig.name = match.captured(1);
                sig.startBit = match.captured(2).toInt();
                sig.bitLength = match.captured(3).toInt();
                sig.isBigEndian = (match.captured(4).toInt() == 0); // 0 = Motorola, 1 = Intel
                sig.isSigned = (match.captured(5) == QStringLiteral("-"));
                sig.factor = match.captured(6).toDouble();
                sig.offset = match.captured(7).toDouble();
                sig.minVal = match.captured(8).toDouble();
                sig.maxVal = match.captured(9).toDouble();
                sig.unit = match.captured(10);
                currentMsg.signalList.append(sig);
            }
        }
    }

    if (hasCurrentMsg) {
        addMessage(currentMsg);
    }

    return !m_messages.isEmpty();
}

double DbcDatabase::decodeSignal(const QByteArray &payload, const DbcSignal &sig) {
    if (payload.isEmpty() || sig.bitLength <= 0) return 0.0;

    quint64 rawVal = 0;
    if (sig.isBigEndian) {
        // Motorola (Big Endian)
        int curBit = sig.startBit;
        for (int i = 0; i < sig.bitLength; ++i) {
            int byteIdx = curBit / 8;
            int bitIdx = curBit % 8;
            if (byteIdx >= 0 && byteIdx < payload.size()) {
                bool bit = (payload.at(byteIdx) >> bitIdx) & 1;
                rawVal = (rawVal << 1) | bit;
            }
            if (bitIdx == 0) {
                curBit = (byteIdx + 1) * 8 + 7;
            } else {
                curBit--;
            }
        }
    } else {
        // Intel (Little Endian)
        for (int i = 0; i < sig.bitLength; ++i) {
            int bitPos = sig.startBit + i;
            int byteIdx = bitPos / 8;
            int bitIdx = bitPos % 8;
            if (byteIdx >= 0 && byteIdx < payload.size()) {
                bool bit = (payload.at(byteIdx) >> bitIdx) & 1;
                rawVal |= (static_cast<quint64>(bit) << i);
            }
        }
    }

    double value = 0.0;
    if (sig.isSigned) {
        qint64 signedVal = 0;
        if (sig.bitLength < 64) {
            quint64 signBit = 1ULL << (sig.bitLength - 1);
            if (rawVal & signBit) {
                signedVal = static_cast<qint64>(rawVal | ~((1ULL << sig.bitLength) - 1));
            } else {
                signedVal = static_cast<qint64>(rawVal);
            }
        } else {
            signedVal = static_cast<qint64>(rawVal);
        }
        value = static_cast<double>(signedVal);
    } else {
        value = static_cast<double>(rawVal);
    }

    return (sig.factor * value) + sig.offset;
}

} // namespace aether
