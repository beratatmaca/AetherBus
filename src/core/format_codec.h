/**
 * @file format_codec.h
 * @brief Pure byte <-> text conversions for the HTerm-style multi-format view.
 *
 * Every function here is free of I/O and global state so it can be exercised
 * directly by the unit tests. The GUI layers formatting on top of the raw bytes
 * delivered by the interception backend.
 */
#pragma once

#include <QByteArray>
#include <QString>

namespace aether::codec {

/**
 * @brief Render bytes as space-separated uppercase hex pairs.
 * @param bytes Raw input bytes.
 * @return e.g. @c "41 42 0D" (empty string for empty input).
 */
QString toHex(const QByteArray &bytes);

/**
 * @brief Render bytes as printable ASCII.
 * @param bytes Raw input bytes.
 * @return Printable characters verbatim; every non-printable byte becomes @c '.'.
 */
QString toAscii(const QByteArray &bytes);

/**
 * @brief Render bytes as ASCII with visible escapes for control bytes.
 * @param bytes Raw input bytes.
 * @return Printable characters verbatim, @c \\r \\n \\t for those controls, and
 *         @c \\xHH for any other non-printable byte. Backs the console's
 *         "show control characters" option.
 */
QString toAsciiEscaped(const QByteArray &bytes);

/**
 * @brief Render bytes as space-separated 8-bit binary groups.
 * @param bytes Raw input bytes.
 * @return e.g. @c "01000001 01000010".
 */
QString toBinary(const QByteArray &bytes);

/**
 * @brief Render bytes as space-separated zero-padded decimal values.
 * @param bytes Raw input bytes.
 * @return e.g. @c "065 066".
 */
QString toDecimal(const QByteArray &bytes);

/**
 * @brief Parse a space-separated hex string (e.g. @c "41 42 0d") into bytes.
 *
 * Whitespace between tokens is flexible and each token must be one or two hex
 * digits.
 * @param[in]  text     User-entered hex text.
 * @param[out] out      Receives the parsed bytes on success; untouched on failure.
 * @param[out] errorPos Optional; receives the index of the first bad token.
 * @return @c true on success, @c false on any malformed token.
 */
bool parseHexString(const QString &text, QByteArray &out, int *errorPos = nullptr);

/**
 * @brief Parse space-separated decimal byte values (e.g. @c "65 66 13").
 *
 * Each token must be in 0..255. Same contract as @ref parseHexString.
 * @param[in]  text     User-entered decimal text.
 * @param[out] out      Receives the parsed bytes on success.
 * @param[out] errorPos Optional; receives the index of the first bad token.
 * @return @c true on success, @c false on any out-of-range/invalid token.
 */
bool parseDecString(const QString &text, QByteArray &out, int *errorPos = nullptr);

/**
 * @brief Parse space-separated binary octets (e.g. @c "01000001 1010").
 *
 * Each token is 1..8 binary digits with a value in 0..255. Same contract as
 * @ref parseHexString.
 * @param[in]  text     User-entered binary text.
 * @param[out] out      Receives the parsed bytes on success.
 * @param[out] errorPos Optional; receives the index of the first bad token.
 * @return @c true on success, @c false on any malformed token.
 */
bool parseBinString(const QString &text, QByteArray &out, int *errorPos = nullptr);

}  // namespace aether::codec
