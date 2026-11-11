/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "platform_text.h"
#include "three_mm/text.h"
#if defined(_WIN32) && !defined(KAUAI_PORTABLE_TEXT)
#define NOMINMAX
#include <windows.h>
#define KAUAI_NATIVE_TEXT
#endif

namespace {
#ifdef KAUAI_NATIVE_TEXT
std::size_t Decode(void *, const void *input, std::size_t bytes, int source, unsigned short *output, std::size_t capacity)
{
    if (!bytes) return 0;
    UINT page = source == THREE_MM_TEXT_WINDOWS ? CP_ACP : CP_MACCP;
    DWORD flags = page == CP_ACP && GetACP() == CP_UTF8 ? 0 : MB_PRECOMPOSED;
    int needed = MultiByteToWideChar(page, flags, static_cast<const char *>(input), static_cast<int>(bytes), nullptr, 0);
    if (!needed || !output) return needed;
    if (capacity < static_cast<std::size_t>(needed)) return 0;
    return MultiByteToWideChar(page, flags, static_cast<const char *>(input), static_cast<int>(bytes), reinterpret_cast<wchar_t *>(output), needed);
}
std::size_t Encode(void *, const unsigned short *input, std::size_t count, char *output, std::size_t capacity)
{
    int needed = WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<const wchar_t *>(input), static_cast<int>(count), nullptr, 0, nullptr, nullptr);
    if (!needed || !output) return needed;
    if (capacity < static_cast<std::size_t>(needed)) return 0;
    return WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<const wchar_t *>(input), static_cast<int>(count), output, needed, nullptr, nullptr);
}
const ThreeMMTextHost nativeText = {nullptr, Decode, Encode};
const ThreeMMTextHost *Host() { return &nativeText; }
#else
const ThreeMMTextHost *Host() { return nullptr; }
#endif
} // namespace
std::size_t KauaiText::ToUtf16(const void *input, std::size_t bytes, Encoding source, std::uint16_t *output, std::size_t capacity)
{
    return three_mm_text_utf16(Host(), input, bytes, static_cast<int>(source), output, capacity);
}
std::size_t KauaiText::ToAnsi(const void *input, std::size_t bytes, Encoding source, char *output, std::size_t capacity)
{
    return three_mm_text_ansi(Host(), input, bytes, static_cast<int>(source), output, capacity);
}

void KauaiText::UpperBytes(char *text, std::size_t count)
{
#ifdef KAUAI_NATIVE_TEXT
    CharUpperBuffA(text, static_cast<DWORD>(count));
#else
    three_mm_text_case_bytes(text, count, 1);
#endif
}
void KauaiText::LowerBytes(char *text, std::size_t count)
{
#ifdef KAUAI_NATIVE_TEXT
    CharLowerBuffA(text, static_cast<DWORD>(count));
#else
    three_mm_text_case_bytes(text, count, 0);
#endif
}
namespace
{
struct ByteMaps
{
    char upper[256], lower[256];
    ByteMaps()
    {
        for (unsigned i = 0; i < 256; ++i)
            upper[i] = lower[i] = static_cast<char>(i);
        KauaiText::UpperBytes(upper, 256);
        KauaiText::LowerBytes(lower, 256);
    }
};
const ByteMaps &Maps()
{
    static const ByteMaps maps;
    return maps;
}
} // namespace
void KauaiText::UpperMappedBytes(char *text, std::size_t count)
{
    const auto &maps = Maps();
    for (std::size_t i = 0; i < count; ++i)
        text[i] = maps.upper[static_cast<unsigned char>(text[i])];
}
void KauaiText::LowerMappedBytes(char *text, std::size_t count)
{
    const auto &maps = Maps();
    for (std::size_t i = 0; i < count; ++i)
        text[i] = maps.lower[static_cast<unsigned char>(text[i])];
}
void KauaiText::UpperUtf16(std::uint16_t *text, std::size_t count)
{
#ifdef KAUAI_NATIVE_TEXT
    CharUpperBuffW(reinterpret_cast<wchar_t *>(text), static_cast<DWORD>(count));
#else
    three_mm_text_case_utf16(text, count, 1);
#endif
}
void KauaiText::LowerUtf16(std::uint16_t *text, std::size_t count)
{
#ifdef KAUAI_NATIVE_TEXT
    CharLowerBuffW(reinterpret_cast<wchar_t *>(text), static_cast<DWORD>(count));
#else
    three_mm_text_case_utf16(text, count, 0);
#endif
}
int KauaiText::CompareBytes(const char *a, std::size_t na, const char *b, std::size_t nb, bool ignoreCase)
{
    if (!na || !nb)
        return na == nb ? 0 : na < nb ? -1 : 1;
#ifdef KAUAI_NATIVE_TEXT
    if (na > INT32_MAX || nb > INT32_MAX)
        return 2;
    int result = CompareStringA(LOCALE_USER_DEFAULT, ignoreCase ? NORM_IGNORECASE : 0, a, static_cast<int>(na), b,
                                static_cast<int>(nb));
    return result ? result - CSTR_EQUAL : 2;
#else
    return three_mm_text_compare(a, na, b, nb, 0, ignoreCase);
#endif
}
int KauaiText::CompareUtf16(const std::uint16_t *a, std::size_t na, const std::uint16_t *b, std::size_t nb,
                            bool ignoreCase)
{
    if (!na || !nb)
        return na == nb ? 0 : na < nb ? -1 : 1;
#ifdef KAUAI_NATIVE_TEXT
    if (na > INT32_MAX || nb > INT32_MAX)
        return 2;
    int result =
        CompareStringW(LOCALE_USER_DEFAULT, ignoreCase ? NORM_IGNORECASE : 0, reinterpret_cast<const wchar_t *>(a),
                       static_cast<int>(na), reinterpret_cast<const wchar_t *>(b), static_cast<int>(nb));
    return result ? result - CSTR_EQUAL : 2;
#else
    return three_mm_text_compare(a, na, b, nb, 1, ignoreCase);
#endif
}
