#include "core/format_codec.h"

#include <QRegularExpression>
#include <QStringList>

namespace aether::codec {

QString toHex(const QByteArray &bytes) {
    QString out;
    out.reserve(bytes.size() * 3);
    for (int i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            out += QLatin1Char(' ');
        }
        const auto b = static_cast<unsigned char>(bytes.at(i));
        out += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

QString toAscii(const QByteArray &bytes) {
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        out += (b >= 0x20 && b < 0x7F) ? QLatin1Char(static_cast<char>(b)) : QLatin1Char('.');
    }
    return out;
}

QString toAsciiEscaped(const QByteArray &bytes) {
    QString out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        if (b >= 0x20 && b < 0x7F) {
            out += QLatin1Char(static_cast<char>(b));
        } else if (b == '\r') {
            out += QStringLiteral("\\r");
        } else if (b == '\n') {
            out += QStringLiteral("\\n");
        } else if (b == '\t') {
            out += QStringLiteral("\\t");
        } else {
            const QString hex = QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
            out += QStringLiteral("\\x") + hex;
        }
    }
    return out;
}

QString toBinary(const QByteArray &bytes) {
    QString out;
    out.reserve(bytes.size() * 9);
    for (int i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            out += QLatin1Char(' ');
        }
        const auto b = static_cast<unsigned char>(bytes.at(i));
        out += QStringLiteral("%1").arg(b, 8, 2, QLatin1Char('0'));
    }
    return out;
}

QString toDecimal(const QByteArray &bytes) {
    QString out;
    out.reserve(bytes.size() * 4);
    for (int i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            out += QLatin1Char(' ');
        }
        const auto b = static_cast<unsigned char>(bytes.at(i));
        out += QStringLiteral("%1").arg(b, 3, 10, QLatin1Char('0'));
    }
    return out;
}

bool parseHexString(const QString &text, QByteArray &out, int *errorPos) {
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

    QByteArray parsed;
    parsed.reserve(tokens.size());
    for (int i = 0; i < tokens.size(); ++i) {
        const QString &tok = tokens.at(i);
        if (tok.size() < 1 || tok.size() > 2) {
            if (errorPos != nullptr) {
                *errorPos = i;
            }
            return false;
        }
        bool ok = false;
        const uint value = tok.toUInt(&ok, 16);
        if (!ok || value > 0xFF) {
            if (errorPos != nullptr) {
                *errorPos = i;
            }
            return false;
        }
        parsed.append(static_cast<char>(value));
    }

    out = parsed;
    return true;
}

bool parseDecString(const QString &text, QByteArray &out, int *errorPos) {
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

    QByteArray parsed;
    parsed.reserve(tokens.size());
    for (int i = 0; i < tokens.size(); ++i) {
        bool ok = false;
        const uint value = tokens.at(i).toUInt(&ok, 10);
        if (!ok || value > 0xFF) {
            if (errorPos != nullptr) {
                *errorPos = i;
            }
            return false;
        }
        parsed.append(static_cast<char>(value));
    }

    out = parsed;
    return true;
}

bool parseBinString(const QString &text, QByteArray &out, int *errorPos) {
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

    QByteArray parsed;
    parsed.reserve(tokens.size());
    for (int i = 0; i < tokens.size(); ++i) {
        const QString &tok = tokens.at(i);
        bool ok = false;
        const uint value = tok.toUInt(&ok, 2);
        if (!ok || tok.size() > 8 || value > 0xFF) {
            if (errorPos != nullptr) {
                *errorPos = i;
            }
            return false;
        }
        parsed.append(static_cast<char>(value));
    }

    out = parsed;
    return true;
}

}  // namespace aether::codec
