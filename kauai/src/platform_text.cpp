/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "platform_text.h"
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>
#include "legacy_text_maps.h"
#include "chtrans.h"
#if defined(_WIN32) && !defined(KAUAI_PORTABLE_TEXT)
#define NOMINMAX
#include <windows.h>
#define KAUAI_NATIVE_TEXT
#endif

namespace
{
using namespace KauaiText;
bool Wide(Encoding encoding)
{
    return encoding == Encoding::Utf16LE || encoding == Encoding::Utf16BE;
}
std::uint16_t Unit(const unsigned char *bytes, Encoding encoding)
{
    return encoding == Encoding::Utf16LE ? bytes[0] | (bytes[1] << 8) : (bytes[0] << 8) | bytes[1];
}
std::uint16_t MapUnit(std::uint16_t unit, bool upper)
{
    // Portable policy is deliberately bounded to the application's legacy
    // Windows repertoire, not an unspecified process Unicode locale.
    for (unsigned i = 0; i < 256; ++i)
        if (windows1252[i] == unit)
            return windows1252[upper ? upper1252[i] : lower1252[i]];
    return unit;
}
template <class Char> int Ordinal(const Char *a, std::size_t na, const Char *b, std::size_t nb, bool ignoreCase)
{
    for (std::size_t i = 0; i < na && i < nb; ++i)
    {
        auto left = static_cast<std::uint16_t>(a[i]);
        auto right = static_cast<std::uint16_t>(b[i]);
        if (ignoreCase)
        {
            left = MapUnit(left, true);
            right = MapUnit(right, true);
        }
        if (left != right)
            return left < right ? -1 : 1;
    }
    return na == nb ? 0 : na < nb ? -1 : 1;
}
} // namespace

std::size_t KauaiText::ToUtf16(const void *input, std::size_t bytes, Encoding source, std::uint16_t *output,
                               std::size_t capacity)
{
    if ((!input && bytes) || bytes > INT32_MAX)
        return 0;
    const auto *data = static_cast<const unsigned char *>(input);
    if (Wide(source))
    {
        if (bytes % 2)
            return 0;
        const auto count = bytes / 2;
        if (!output)
            return count;
        if (capacity < count)
            return 0;
        for (std::size_t i = 0; i < count; ++i)
            output[i] = Unit(data + i * 2, source);
        return count;
    }
    if (source != Encoding::WindowsAnsi && source != Encoding::MacRoman)
        return 0;
#ifdef KAUAI_NATIVE_TEXT
    if (!bytes)
        return 0;
    UINT page = source == Encoding::WindowsAnsi ? CP_ACP : CP_MACCP;
    DWORD flags = page == CP_ACP && GetACP() == CP_UTF8 ? 0 : MB_PRECOMPOSED;
    int needed =
        MultiByteToWideChar(page, flags, static_cast<const char *>(input), static_cast<int>(bytes), nullptr, 0);
    if (!needed || !output)
        return needed;
    if (capacity < static_cast<std::size_t>(needed))
        return 0;
    return MultiByteToWideChar(page, flags, static_cast<const char *>(input), static_cast<int>(bytes),
                               reinterpret_cast<wchar_t *>(output), needed);
#else
    if (!output)
        return bytes;
    if (capacity < bytes)
        return 0;
    const auto *map = source == Encoding::WindowsAnsi ? windows1252 : macRoman;
    for (std::size_t i = 0; i < bytes; ++i)
        output[i] = map[data[i]];
    return bytes;
#endif
}

std::size_t KauaiText::ToAnsi(const void *input, std::size_t bytes, Encoding source, char *output, std::size_t capacity)
{
    if ((!input && bytes) || bytes > INT32_MAX)
        return 0;
    const auto *data = static_cast<const unsigned char *>(input);
    if (source == Encoding::WindowsAnsi || source == Encoding::MacRoman)
    {
        if (!output)
            return bytes;
        if (capacity < bytes)
            return 0;
        for (std::size_t i = 0; i < bytes; ++i)
            output[i] = static_cast<char>(
                source == Encoding::MacRoman && data[i] >= 128 ? _mpchschsMacToWin[data[i] - 128] : data[i]);
        return bytes;
    }
    if (!Wide(source) || bytes % 2)
        return 0;
    try
    {
        std::vector<std::uint16_t> units(bytes / 2);
        if (bytes && !ToUtf16(input, bytes, source, units.data(), units.size()))
            return 0;
#ifdef KAUAI_NATIVE_TEXT
        if (!bytes)
            return 0;
        int needed = WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<const wchar_t *>(units.data()),
                                         static_cast<int>(units.size()), nullptr, 0, nullptr, nullptr);
        if (!needed || !output)
            return needed;
        if (capacity < static_cast<std::size_t>(needed))
            return 0;
        return WideCharToMultiByte(CP_ACP, 0, reinterpret_cast<const wchar_t *>(units.data()),
                                   static_cast<int>(units.size()), output, needed, nullptr, nullptr);
#else
        std::vector<char> converted;
        converted.reserve(units.size());
        for (std::size_t i = 0; i < units.size(); ++i)
        {
            unsigned mapped = 256;
            for (unsigned b = 0; b < 256; ++b)
                if (windows1252[b] == units[i])
                {
                    mapped = b;
                    break;
                }
            converted.push_back(mapped < 256 ? static_cast<char>(mapped) : '?');
            if (units[i] >= 0xd800 && units[i] <= 0xdbff && i + 1 < units.size() && units[i + 1] >= 0xdc00 &&
                units[i + 1] <= 0xdfff)
                ++i;
        }
        if (!output)
            return converted.size();
        if (capacity < converted.size())
            return 0;
        if (!converted.empty())
            std::memcpy(output, converted.data(), converted.size());
        return converted.size();
#endif
    }
    catch (...)
    {
        return 0;
    }
}

void KauaiText::UpperBytes(char *text, std::size_t count)
{
#ifdef KAUAI_NATIVE_TEXT
    CharUpperBuffA(text, static_cast<DWORD>(count));
#else
    for (std::size_t i = 0; i < count; ++i)
        text[i] = static_cast<char>(upper1252[static_cast<unsigned char>(text[i])]);
#endif
}
void KauaiText::LowerBytes(char *text, std::size_t count)
{
#ifdef KAUAI_NATIVE_TEXT
    CharLowerBuffA(text, static_cast<DWORD>(count));
#else
    for (std::size_t i = 0; i < count; ++i)
        text[i] = static_cast<char>(lower1252[static_cast<unsigned char>(text[i])]);
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
        UpperBytes(upper, 256);
        LowerBytes(lower, 256);
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
    for (std::size_t i = 0; i < count; ++i)
        text[i] = MapUnit(text[i], true);
#endif
}
void KauaiText::LowerUtf16(std::uint16_t *text, std::size_t count)
{
#ifdef KAUAI_NATIVE_TEXT
    CharLowerBuffW(reinterpret_cast<wchar_t *>(text), static_cast<DWORD>(count));
#else
    for (std::size_t i = 0; i < count; ++i)
        text[i] = MapUnit(text[i], false);
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
    // Compare bytes by their decoded CP1252 value, with explicit case mapping.
    for (std::size_t i = 0; i < na && i < nb; ++i)
    {
        auto left = static_cast<unsigned char>(a[i]), right = static_cast<unsigned char>(b[i]);
        if (ignoreCase)
        {
            left = upper1252[left];
            right = upper1252[right];
        }
        if (left != right)
            return windows1252[left] < windows1252[right] ? -1 : 1;
    }
    return na == nb ? 0 : na < nb ? -1 : 1;
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
    return Ordinal(a, na, b, nb, ignoreCase);
#endif
}
