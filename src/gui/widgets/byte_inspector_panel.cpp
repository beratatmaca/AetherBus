#include "gui/widgets/byte_inspector_panel.hpp"

#include <QFontDatabase>
#include <QLabel>
#include <QStringList>
#include <QVBoxLayout>
#include <QVector>

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

    struct Row {
        QString tag;
        bool isFloat = false;
        bool hasBe = false;
        QString uLe, sLe, uBe, sBe;  ///< integer readings
        QString fLe, fBe;            ///< float readings
    };
    QVector<Row> rows;

    {
        Row r;
        r.tag = QStringLiteral("int8");
        r.uLe = QString::number(static_cast<quint8>(m_bytes.at(0)));
        r.sLe = QString::number(static_cast<qint8>(m_bytes.at(0)));
        rows.push_back(r);
    }
    if (len >= 2) {
        Row r;
        r.tag = QStringLiteral("int16");
        r.hasBe = true;
        r.uLe = QString::number(readU16(m_bytes, false));
        r.sLe = QString::number(static_cast<qint16>(readU16(m_bytes, false)));
        r.uBe = QString::number(readU16(m_bytes, true));
        r.sBe = QString::number(static_cast<qint16>(readU16(m_bytes, true)));
        rows.push_back(r);
    }
    if (len >= 4) {
        Row r;
        r.tag = QStringLiteral("int32");
        r.hasBe = true;
        r.uLe = QString::number(readU32(m_bytes, false));
        r.sLe = QString::number(static_cast<qint32>(readU32(m_bytes, false)));
        r.uBe = QString::number(readU32(m_bytes, true));
        r.sBe = QString::number(static_cast<qint32>(readU32(m_bytes, true)));
        rows.push_back(r);

        Row f;
        f.tag = QStringLiteral("f32");
        f.isFloat = true;
        f.hasBe = true;
        f.fLe = QString::number(readFloat(m_bytes, false), 'g', 6);
        f.fBe = QString::number(readFloat(m_bytes, true), 'g', 6);
        rows.push_back(f);
    }
    if (len >= 8) {
        Row r;
        r.tag = QStringLiteral("int64");
        r.hasBe = true;
        r.uLe = QString::number(readU64(m_bytes, false));
        r.sLe = QString::number(static_cast<qint64>(readU64(m_bytes, false)));
        r.uBe = QString::number(readU64(m_bytes, true));
        r.sBe = QString::number(static_cast<qint64>(readU64(m_bytes, true)));
        rows.push_back(r);

        Row f;
        f.tag = QStringLiteral("f64");
        f.isFloat = true;
        f.hasBe = true;
        f.fLe = QString::number(readDouble(m_bytes, false), 'g', 10);
        f.fBe = QString::number(readDouble(m_bytes, true), 'g', 10);
        rows.push_back(f);
    }

    // Widths for the "u <unsigned>  s <signed>" sub-columns, shared across LE/BE.
    int uW = 0;
    int sW = 0;
    for (const Row &r : rows) {
        if (r.isFloat) {
            continue;
        }
        uW = qMax(uW, qMax(static_cast<int>(r.uLe.length()), static_cast<int>(r.uBe.length())));
        sW = qMax(sW, qMax(static_cast<int>(r.sLe.length()), static_cast<int>(r.sBe.length())));
    }
    const auto intCell = [uW, sW](const QString &u, const QString &s) {
        return QStringLiteral("u %1  s %2").arg(u.leftJustified(uW), s.leftJustified(sW));
    };

    const QString leHeader = QStringLiteral("Little-Endian");
    const QString beHeader = QStringLiteral("Big-Endian");
    const bool anyBe = len >= 2;

    int tagW = 0;
    int leW = anyBe ? static_cast<int>(leHeader.length()) : 0;
    for (const Row &r : rows) {
        tagW = qMax(tagW, static_cast<int>(r.tag.length()));
        const QString leCell = r.isFloat ? r.fLe : intCell(r.uLe, r.sLe);
        leW = qMax(leW, static_cast<int>(leCell.length()));
    }

    QStringList lines;
    lines << QStringLiteral("▸ %1 byte%2").arg(len).arg(len == 1 ? QString() : QStringLiteral("s"));
    if (anyBe) {
        lines << QStringLiteral("%1  %2  %3").arg(QString(tagW, QLatin1Char(' ')), leHeader.leftJustified(leW), beHeader);
    }
    for (const Row &r : rows) {
        const QString leCell = r.isFloat ? r.fLe : intCell(r.uLe, r.sLe);
        QString line = r.tag.leftJustified(tagW) + QStringLiteral("  ") + (anyBe ? leCell.leftJustified(leW) : leCell);
        if (r.hasBe) {
            line += QStringLiteral("  ") + (r.isFloat ? r.fBe : intCell(r.uBe, r.sBe));
        }
        lines << line;
    }

    return lines.join(QLatin1Char('\n'));
}

}  // namespace aether
