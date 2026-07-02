#include "gui/widgets/byte_inspector_panel.hpp"
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace aether {

ByteInspectorPanel::ByteInspectorPanel(QWidget *parent) : QWidget(parent) {
    setupUi();
}

ByteInspectorPanel::~ByteInspectorPanel() = default;

void ByteInspectorPanel::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_infoLabel = new QLabel(QStringLiteral("Select bytes in the console to inspect..."), this);
    m_infoLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #888;"));
    layout->addWidget(m_infoLabel);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setRowCount(9);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("Type"), QStringLiteral("Little Endian (Intel)"), QStringLiteral("Big Endian (Motorola)")});

    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    QStringList types = {QStringLiteral("Binary (bits)"),
                         QStringLiteral("ASCII / Plain Text"),
                         QStringLiteral("8-bit Integer (Int8 / Uint8)"),
                         QStringLiteral("16-bit Integer (Int16 / Uint16)"),
                         QStringLiteral("32-bit Integer (Int32 / Uint32)"),
                         QStringLiteral("64-bit Integer (Int64 / Uint64)"),
                         QStringLiteral("Float (32-bit)"),
                         QStringLiteral("Double (64-bit)"),
                         QStringLiteral("Hexadecimal Sequence")};

    for (int i = 0; i < types.size(); ++i) {
        auto *typeItem = new QTableWidgetItem(types.at(i));
        typeItem->setFont(QFont(QStringLiteral("monospace")));
        m_table->setItem(i, 0, typeItem);

        auto *leItem = new QTableWidgetItem(QStringLiteral("---"));
        leItem->setFont(QFont(QStringLiteral("monospace")));
        m_table->setItem(i, 1, leItem);

        auto *beItem = new QTableWidgetItem(QStringLiteral("---"));
        beItem->setFont(QFont(QStringLiteral("monospace")));
        m_table->setItem(i, 2, beItem);
    }

    layout->addWidget(m_table, 1);
}

void ByteInspectorPanel::setBytes(const QByteArray &bytes) {
    m_bytes = bytes;
    if (m_bytes.isEmpty()) {
        m_infoLabel->setText(QStringLiteral("No bytes selected."));
        for (int i = 0; i < m_table->rowCount(); ++i) {
            m_table->item(i, 1)->setText(QStringLiteral("---"));
            m_table->item(i, 2)->setText(QStringLiteral("---"));
        }
        return;
    }

    m_infoLabel->setText(QStringLiteral("Inspecting %1 bytes:").arg(m_bytes.size()));
    updateTableValues();
}

static QString toBinaryString(const QByteArray &bytes) {
    QStringList list;
    for (char byte : bytes) {
        list.append(QString("%1").arg(static_cast<quint8>(byte), 8, 2, QLatin1Char('0')));
    }
    return list.join(QLatin1Char(' '));
}

static QString toAsciiString(const QByteArray &bytes) {
    QString result;
    for (char ch : bytes) {
        if (ch >= 32 && ch <= 126) {
            result.append(ch);
        } else {
            result.append(QLatin1Char('.'));
        }
    }
    return result;
}

static float readFloat(const QByteArray &bytes, bool bigEndian) {
    union {
        float f;
        quint32 u;
    } val;
    if (bigEndian) {
        val.u = (static_cast<quint8>(bytes[0]) << 24) | (static_cast<quint8>(bytes[1]) << 16) | (static_cast<quint8>(bytes[2]) << 8) |
                (static_cast<quint8>(bytes[3]));
    } else {
        val.u = (static_cast<quint8>(bytes[0])) | (static_cast<quint8>(bytes[1]) << 8) | (static_cast<quint8>(bytes[2]) << 16) |
                (static_cast<quint8>(bytes[3]) << 24);
    }
    return val.f;
}

static double readDouble(const QByteArray &bytes, bool bigEndian) {
    union {
        double d;
        quint64 u;
    } val;
    if (bigEndian) {
        val.u = (static_cast<quint64>(static_cast<quint8>(bytes[0])) << 56) | (static_cast<quint64>(static_cast<quint8>(bytes[1])) << 48) |
                (static_cast<quint64>(static_cast<quint8>(bytes[2])) << 40) | (static_cast<quint64>(static_cast<quint8>(bytes[3])) << 32) |
                (static_cast<quint64>(static_cast<quint8>(bytes[4])) << 24) | (static_cast<quint64>(static_cast<quint8>(bytes[5])) << 16) |
                (static_cast<quint64>(static_cast<quint8>(bytes[6])) << 8) | (static_cast<quint64>(static_cast<quint8>(bytes[7])));
    } else {
        val.u = (static_cast<quint64>(static_cast<quint8>(bytes[0]))) | (static_cast<quint64>(static_cast<quint8>(bytes[1])) << 8) |
                (static_cast<quint64>(static_cast<quint8>(bytes[2])) << 16) | (static_cast<quint64>(static_cast<quint8>(bytes[3])) << 24) |
                (static_cast<quint64>(static_cast<quint8>(bytes[4])) << 32) | (static_cast<quint64>(static_cast<quint8>(bytes[5])) << 40) |
                (static_cast<quint64>(static_cast<quint8>(bytes[6])) << 48) | (static_cast<quint64>(static_cast<quint8>(bytes[7])) << 56);
    }
    return val.d;
}

void ByteInspectorPanel::updateTableValues() {
    int len = m_bytes.size();

    // 0. Binary
    m_table->item(0, 1)->setText(toBinaryString(m_bytes));
    m_table->item(0, 2)->setText(toBinaryString(m_bytes));

    // 1. ASCII
    m_table->item(1, 1)->setText(toAsciiString(m_bytes));
    m_table->item(1, 2)->setText(toAsciiString(m_bytes));

    // 2. 8-bit Integer (Int8 / Uint8)
    if (len >= 1) {
        auto i8 = static_cast<qint8>(m_bytes.at(0));
        auto u8 = static_cast<quint8>(m_bytes.at(0));
        QString leText = QStringLiteral("Signed: %1 | Unsigned: %2").arg(i8).arg(u8);
        m_table->item(2, 1)->setText(leText);
        m_table->item(2, 2)->setText(leText);  // 8-bit endianness is same
    } else {
        m_table->item(2, 1)->setText(QStringLiteral("N/A"));
        m_table->item(2, 2)->setText(QStringLiteral("N/A"));
    }

    // 3. 16-bit Integer (Int16 / Uint16)
    if (len >= 2) {
        quint16 u16le = static_cast<quint8>(m_bytes[0]) | (static_cast<quint8>(m_bytes[1]) << 8);
        auto i16le = static_cast<qint16>(u16le);

        quint16 u16be = (static_cast<quint8>(m_bytes[0]) << 8) | static_cast<quint8>(m_bytes[1]);
        auto i16be = static_cast<qint16>(u16be);

        m_table->item(3, 1)->setText(QStringLiteral("Signed: %1 | Unsigned: %2").arg(i16le).arg(u16le));
        m_table->item(3, 2)->setText(QStringLiteral("Signed: %1 | Unsigned: %2").arg(i16be).arg(u16be));
    } else {
        m_table->item(3, 1)->setText(QStringLiteral("N/A (needs 2 bytes)"));
        m_table->item(3, 2)->setText(QStringLiteral("N/A (needs 2 bytes)"));
    }

    // 4. 32-bit Integer (Int32 / Uint32)
    if (len >= 4) {
        quint32 u32le = (static_cast<quint8>(m_bytes[0])) | (static_cast<quint8>(m_bytes[1]) << 8) |
                        (static_cast<quint8>(m_bytes[2]) << 16) | (static_cast<quint8>(m_bytes[3]) << 24);
        auto i32le = static_cast<qint32>(u32le);

        quint32 u32be = (static_cast<quint8>(m_bytes[0]) << 24) | (static_cast<quint8>(m_bytes[1]) << 16) |
                        (static_cast<quint8>(m_bytes[2]) << 8) | (static_cast<quint8>(m_bytes[3]));
        auto i32be = static_cast<qint32>(u32be);

        m_table->item(4, 1)->setText(QStringLiteral("Signed: %1 | Unsigned: %2").arg(i32le).arg(u32le));
        m_table->item(4, 2)->setText(QStringLiteral("Signed: %1 | Unsigned: %2").arg(i32be).arg(u32be));
    } else {
        m_table->item(4, 1)->setText(QStringLiteral("N/A (needs 4 bytes)"));
        m_table->item(4, 2)->setText(QStringLiteral("N/A (needs 4 bytes)"));
    }

    // 5. 64-bit Integer (Int64 / Uint64)
    if (len >= 8) {
        quint64 u64le =
            (static_cast<quint64>(static_cast<quint8>(m_bytes[0]))) | (static_cast<quint64>(static_cast<quint8>(m_bytes[1])) << 8) |
            (static_cast<quint64>(static_cast<quint8>(m_bytes[2])) << 16) | (static_cast<quint64>(static_cast<quint8>(m_bytes[3])) << 24) |
            (static_cast<quint64>(static_cast<quint8>(m_bytes[4])) << 32) | (static_cast<quint64>(static_cast<quint8>(m_bytes[5])) << 40) |
            (static_cast<quint64>(static_cast<quint8>(m_bytes[6])) << 48) | (static_cast<quint64>(static_cast<quint8>(m_bytes[7])) << 56);
        auto i64le = static_cast<qint64>(u64le);

        quint64 u64be =
            (static_cast<quint64>(static_cast<quint8>(m_bytes[0])) << 56) | (static_cast<quint64>(static_cast<quint8>(m_bytes[1])) << 48) |
            (static_cast<quint64>(static_cast<quint8>(m_bytes[2])) << 40) | (static_cast<quint64>(static_cast<quint8>(m_bytes[3])) << 32) |
            (static_cast<quint64>(static_cast<quint8>(m_bytes[4])) << 24) | (static_cast<quint64>(static_cast<quint8>(m_bytes[5])) << 16) |
            (static_cast<quint64>(static_cast<quint8>(m_bytes[6])) << 8) | (static_cast<quint64>(static_cast<quint8>(m_bytes[7])));
        auto i64be = static_cast<qint64>(u64be);

        m_table->item(5, 1)->setText(QStringLiteral("Signed: %1 | Unsigned: %2").arg(i64le).arg(u64le));
        m_table->item(5, 2)->setText(QStringLiteral("Signed: %1 | Unsigned: %2").arg(i64be).arg(u64be));
    } else {
        m_table->item(5, 1)->setText(QStringLiteral("N/A (needs 8 bytes)"));
        m_table->item(5, 2)->setText(QStringLiteral("N/A (needs 8 bytes)"));
    }

    // 6. Float
    if (len >= 4) {
        float fLe = readFloat(m_bytes, false);
        float fBe = readFloat(m_bytes, true);
        m_table->item(6, 1)->setText(QString::number(fLe, 'g', 6));
        m_table->item(6, 2)->setText(QString::number(fBe, 'g', 6));
    } else {
        m_table->item(6, 1)->setText(QStringLiteral("N/A (needs 4 bytes)"));
        m_table->item(6, 2)->setText(QStringLiteral("N/A (needs 4 bytes)"));
    }

    // 7. Double
    if (len >= 8) {
        double dLe = readDouble(m_bytes, false);
        double dBe = readDouble(m_bytes, true);
        m_table->item(7, 1)->setText(QString::number(dLe, 'g', 10));
        m_table->item(7, 2)->setText(QString::number(dBe, 'g', 10));
    } else {
        m_table->item(7, 1)->setText(QStringLiteral("N/A (needs 8 bytes)"));
        m_table->item(7, 2)->setText(QStringLiteral("N/A (needs 8 bytes)"));
    }

    // 8. Hex
    QString hexStr = m_bytes.toHex(' ').toUpper();
    m_table->item(8, 1)->setText(hexStr);
    m_table->item(8, 2)->setText(hexStr);
}

}  // namespace aether
