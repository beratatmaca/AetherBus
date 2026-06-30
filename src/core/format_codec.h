// Pure byte <-> text conversions for the HTerm-style multi-format viewport.
//
// Every function here is free of I/O and global state so it can be exercised
// directly by the unit tests. The GUI layers formatting on top of the raw
// bytes delivered by the interception backend.
#pragma once

#include <QByteArray>
#include <QString>

namespace aether::codec {

/// Render bytes as space-separated uppercase hex pairs, e.g. "41 42 0D".
QString toHex(const QByteArray &bytes);

/// Render bytes as printable ASCII; non-printable bytes become '.'.
QString toAscii(const QByteArray &bytes);

/// Like toAscii, but render common control bytes as visible escapes
/// (\r \n \t) and any other non-printable byte as \xHH. Used by the console's
/// "show control characters" option.
QString toAsciiEscaped(const QByteArray &bytes);

/// Render bytes as space-separated 8-bit binary groups, e.g. "01000001".
QString toBinary(const QByteArray &bytes);

/// Render bytes as space-separated zero-padded decimal values, e.g. "065 066".
QString toDecimal(const QByteArray &bytes);

/// Parse a user-entered, space-separated hex string ("41 42 0d") into bytes.
///
/// Whitespace between tokens is flexible. Each token must be one or two hex
/// digits. On any malformed token the function returns false and leaves @p out
/// untouched; @p errorPos (if provided) receives the token index that failed.
bool parseHexString(const QString &text, QByteArray &out, int *errorPos = nullptr);

/// Parse space-separated decimal byte values ("65 66 13"). Each token must be
/// in 0..255. Same contract as parseHexString.
bool parseDecString(const QString &text, QByteArray &out, int *errorPos = nullptr);

/// Parse space-separated binary octets ("01000001 1010"). Each token is 1..8
/// binary digits with a value in 0..255. Same contract as parseHexString.
bool parseBinString(const QString &text, QByteArray &out, int *errorPos = nullptr);

}  // namespace aether::codec
