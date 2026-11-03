/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef PLATFORM_TEXT_H
#define PLATFORM_TEXT_H
#include <cstddef>
#include <cstdint>

namespace KauaiText
{
// Values match the stored Kauai OS/encoding markers. No UTF-8 guessing.
enum class Encoding : std::int16_t
{
    MacRoman = 0x0202,
    WindowsAnsi = 0x0303,
    Utf16BE = 0x0404,
    Utf16LE = 0x0505
};
// Counted conversions include embedded nulls. Null output queries the required
// count. A short output buffer fails without writing; input is never modified.
// UTF-16 output consists of native-endian 16-bit units, independent of wchar_t.
std::size_t ToAnsi(const void *input, std::size_t bytes, Encoding source, char *output, std::size_t capacity);
std::size_t ToUtf16(const void *input, std::size_t bytes, Encoding source, std::uint16_t *output, std::size_t capacity);
void UpperBytes(char *text, std::size_t count);
void LowerBytes(char *text, std::size_t count);
// Preserve the legacy per-byte mapping used by ChsUpper/ChsLower, distinct
// from mapping a complete multibyte string on a Windows DBCS code page.
void UpperMappedBytes(char *text, std::size_t count);
void LowerMappedBytes(char *text, std::size_t count);
void UpperUtf16(std::uint16_t *text, std::size_t count);
void LowerUtf16(std::uint16_t *text, std::size_t count);
// -1/0/+1; 2 signals a native comparison error. Equality and collation remain
// distinct contracts. Windows uses existing user-locale collation; portable
// comparison is deterministic, with CP1252 one-byte case mapping.
int CompareBytes(const char *a, std::size_t na, const char *b, std::size_t nb, bool ignoreCase);
int CompareUtf16(const std::uint16_t *a, std::size_t na, const std::uint16_t *b, std::size_t nb, bool ignoreCase);
} // namespace KauaiText
#endif
