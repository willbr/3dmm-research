/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef PLATFORM_TEXT_H
#define PLATFORM_TEXT_H
#include <cstddef>
#include <cstdint>

#include "mm/native_text.h"

namespace KauaiText
{
// Values match the stored Kauai OS/encoding markers. No UTF-8 guessing.
enum class Encoding : std::int16_t
{
    MacRoman = MM_TEXT_MAC,
    WindowsAnsi = MM_TEXT_WINDOWS,
    Utf16BE = MM_TEXT_UTF16_BE,
    Utf16LE = MM_TEXT_UTF16_LE
};
// Borrowed code-page providers for shared C string operations.
inline const MMTextHost *Services() { return mm_native_text_host(); }
// Counted conversions include embedded nulls. Null output queries the required
// count. A short output buffer fails without writing; input is never modified.
// UTF-16 output consists of native-endian 16-bit units, independent of wchar_t.
inline std::size_t ToAnsi(const void *input, std::size_t bytes, Encoding source, char *output, std::size_t capacity)
{
    return mm_text_ansi(Services(), input, bytes, static_cast<int>(source), output, capacity);
}
inline std::size_t ToUtf16(const void *input, std::size_t bytes, Encoding source, std::uint16_t *output, std::size_t capacity)
{
    return mm_text_utf16(Services(), input, bytes, static_cast<int>(source), output, capacity);
}
inline void UpperBytes(char *text, std::size_t count) { mm_native_text_case_bytes(text, count, 1); }
inline void LowerBytes(char *text, std::size_t count) { mm_native_text_case_bytes(text, count, 0); }
// Preserve the legacy per-byte mapping used by ChsUpper/ChsLower, distinct
// from mapping a complete multibyte string on a Windows DBCS code page.
inline void UpperMappedBytes(char *text, std::size_t count) { mm_native_text_case_mapped(text, count, 1); }
inline void LowerMappedBytes(char *text, std::size_t count) { mm_native_text_case_mapped(text, count, 0); }
inline void UpperUtf16(std::uint16_t *text, std::size_t count) { mm_native_text_case_utf16(text, count, 1); }
inline void LowerUtf16(std::uint16_t *text, std::size_t count) { mm_native_text_case_utf16(text, count, 0); }
// -1/0/+1; 2 signals a native comparison error. Equality and collation remain
// distinct contracts. Windows uses existing user-locale collation; portable
// comparison is deterministic, with CP1252 one-byte case mapping.
inline int CompareBytes(const char *a, std::size_t na, const char *b, std::size_t nb, bool ignoreCase)
{
    return mm_native_text_compare(a, na, b, nb, 0, ignoreCase);
}
inline int CompareUtf16(const std::uint16_t *a, std::size_t na, const std::uint16_t *b, std::size_t nb, bool ignoreCase)
{
    return mm_native_text_compare(a, na, b, nb, 1, ignoreCase);
}
} // namespace KauaiText
#endif
