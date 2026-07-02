#include "gui/widgets/byte_inspector_panel.hpp"

#include <QFontDatabase>
#include <QLabel>
#include <QStringList>
#include <QVBoxLayout>

namespace aether {

namespace {

float readFloat(const QByteArray &bytes, bool bigEndian) {
    union {
        float f;
        quint32 u;
    } val;
    if (bigEndian) {
        val.u = (static_cast<quint32>(static_cast<quint8>(bytes[0])) << 24) | (static_cast<quint32>(static_cast<quint8>(bytes[1])) << 16) |
                (static_cast<quint32>(static_cast<quint8>(bytes[2])) << 8) | static_cast<quint32>(static_cast<quint8>(bytes[3]));
    } else {
        val.u = static_cast<quint32>(static_cast<quint8>(bytes[0])) | (static_cast<quint32>(static_cast<quint8>(bytes[1])) << 8) |
                (static_cast<quint32>(static_cast<quint8>(bytes[2])) << 16) | (static_cast<quint32>(static_cast<quint8>(bytes[3])) << 24);
    }
    return val.f;
}

double readDouble(const QByteArray &bytes, bool bigEndian) {
    union {
        double d;
        quint64 u;
    } val;
    val.u = 0;
    for (int i = 0; i < 8; ++i) {
        const int shift = bigEndian ? (7 - i) * 8 : i * 8;
        val.u |= static_cast<quint64>(static_cast<quint8>(bytes[i])) << shift;
    }
    return val.d;
}

quint16 readU16(const QByteArray &b, bool bigEndian) {
    return bigEndian ? static_cast<quint16>((static_cast<quint8>(b[0]) << 8) | static_cast<quint8>(b[1]))
                     : static_cast<quint16>(static_cast<quint8>(b[0]) | (static_cast<quint8>(b[1]) << 8));
}

quint32 readU32(const QByteArray &b, bool bigEndian) {
    quint32 u = 0;
    for (int i = 0; i < 4; ++i) {
        const int shift = bigEndian ? (3 - i) * 8 : i * 8;
        u |= static_cast<quint32>(static_cast<quint8>(b[i])) << shift;
    }
    return u;
}

quint64 readU64(const QByteArray &b, bool bigEndian) {
    quint64 u = 0;
    for (int i = 0; i < 8; ++i) {
        const int shift = bigEndian ? (7 - i) * 8 : i * 8;
        u |= static_cast<quint64>(static_cast<quint8>(b[i])) << shift;
    }
    return u;
}

// Format an integer's LE/BE interpretation, appending the signed value in
// parentheses only when the high bit is set (i.e. signed != unsigned).
template <typename U, typename S>
QString intRow(const QString &tag, U le, U be) {
    const auto fmt = [](U v) {
        QString s = QString::number(v);
        const auto signedV = static_cast<S>(v);
        if (signedV < 0) {
            s += QStringLiteral(" (%1)").arg(signedV);
        }
        return s;
    };
    return QStringLiteral("%1 LE %2  BE %3").arg(tag, fmt(le), fmt(be));
}

}  // namespace

ByteInspectorPanel::ByteInspectorPanel(QWidget *parent) : QWidget(parent) {
    setupUi();
}

ByteInspectorPanel::~ByteInspectorPanel() = default;

void ByteInspectorPanel::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(0);

    m_content = new QLabel(this);
    m_content->setObjectName(QStringLiteral("byteInspectorContent"));
    m_content->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_content->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_content->setWordWrap(true);
    m_content->setTextFormat(Qt::PlainText);
    m_content->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(m_content);
}

void ByteInspectorPanel::setBytes(const QByteArray &bytes) {
    m_bytes = bytes;
    m_content->setText(buildText());
}

QString ByteInspectorPanel::buildText() const {
    if (m_bytes.isEmpty()) {
        return {};
    }
    const auto len = static_cast<int>(m_bytes.size());
    QStringList lines;

    // Hex / ASCII / binary are already visible in the console, so the inspector
    // shows only the numeric interpretations. LE/BE are spelled out once (they
    // only appear for multi-byte selections).
    QString header = QStringLiteral("▸ %1 byte%2").arg(len).arg(len == 1 ? QString() : QStringLiteral("s"));
    if (len >= 2) {
        header += QStringLiteral("     LE = Little-Endian (Intel)     BE = Big-Endian (Motorola)");
    }
    lines << header;

    lines << QStringLiteral("u8 %1  i8 %2").arg(static_cast<quint8>(m_bytes.at(0))).arg(static_cast<qint8>(m_bytes.at(0)));

    if (len >= 2) {
        lines << intRow<quint16, qint16>(QStringLiteral("u16"), readU16(m_bytes, false), readU16(m_bytes, true));
    }
    if (len >= 4) {
        lines << intRow<quint32, qint32>(QStringLiteral("u32"), readU32(m_bytes, false), readU32(m_bytes, true));
        lines << QStringLiteral("f32 LE %1  BE %2")
                     .arg(QString::number(readFloat(m_bytes, false), 'g', 6), QString::number(readFloat(m_bytes, true), 'g', 6));
    }
    if (len >= 8) {
        lines << intRow<quint64, qint64>(QStringLiteral("u64"), readU64(m_bytes, false), readU64(m_bytes, true));
        lines << QStringLiteral("f64 LE %1  BE %2")
                     .arg(QString::number(readDouble(m_bytes, false), 'g', 10), QString::number(readDouble(m_bytes, true), 'g', 10));
    }

    return lines.join(QLatin1Char('\n'));
}

}  // namespace aether
