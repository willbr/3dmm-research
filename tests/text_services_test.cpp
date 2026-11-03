/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "platform_text.h"
#include "text_utf8.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#if defined(_WIN32) && !defined(KAUAI_PORTABLE_TEXT)
#define NOMINMAX
#include <windows.h> // Independent oracle for preservation of native behavior.
#define NATIVE_ORACLE
#endif
using namespace KauaiText;
static void CheckAt(bool ok, int line)
{
    if (!ok)
    {
        std::fprintf(stderr, "Text service check failed at line %d\n", line);
        std::exit(1);
    }
}
#define Check(ok) CheckAt((ok), __LINE__)
static std::uint32_t Hash(const char *bytes, unsigned count)
{
    std::uint32_t result = 2166136261u;
    for (unsigned i = 0; i < count; ++i)
        result = (result ^ static_cast<unsigned char>(bytes[i])) * 16777619u;
    return result;
}
int main()
{
    {
        std::string utf8;
        std::vector<std::uint16_t> units = {'A', 0, 0x20AC, 0xD83D, 0xDE00};
        Check(EncodeUtf8(units, utf8) && utf8 == std::string("A\0\xE2\x82\xAC\xF0\x9F\x98\x80", 9));
        std::vector<std::uint16_t> decoded;
        Check(DecodeUtf8(utf8, decoded) && decoded == units);
        Check(DecodeUtf8("", decoded) && decoded.empty());
        Check(EncodeUtf8(decoded, utf8) && utf8.empty());
        for (const auto &bad : {"\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82"})
            Check(!DecodeUtf8(bad, decoded));
        Check(!EncodeUtf8({0xD800}, utf8));
        Check(!EncodeUtf8({0xDC00}, utf8));
    }
    const unsigned char be[] = {0, 'A', 0, 0, 0, 'Z'};
    const unsigned char le[] = {'A', 0, 0, 0, 'Z', 0};
    std::uint16_t units[256] = {};
    char bytes[2048] = {};
    Check(ToUtf16(be, sizeof(be), Encoding::Utf16BE, nullptr, 0) == 3);
    Check(ToUtf16(be, sizeof(be), Encoding::Utf16BE, units, 256) == 3);
    Check(units[0] == 'A' && units[1] == 0 && units[2] == 'Z');
    Check(ToAnsi(be, sizeof(be), Encoding::Utf16BE, bytes, sizeof(bytes)) == 3);
    Check(bytes[0] == 'A' && bytes[1] == 0 && bytes[2] == 'Z');
    Check(ToAnsi(le, sizeof(le), Encoding::Utf16LE, bytes, sizeof(bytes)) == 3);
    std::memset(bytes, 0x17, sizeof(bytes));
    Check(ToAnsi(be, sizeof(be), Encoding::Utf16BE, bytes, 2) == 0 && bytes[0] == 0x17 && bytes[2] == 0x17);
    units[0] = units[1] = units[2] = 0x1717;
    Check(ToUtf16(be, sizeof(be), Encoding::Utf16BE, units, 2) == 0 && units[0] == 0x1717 && units[2] == 0x1717);
    Check(!ToAnsi(be, 5, Encoding::Utf16BE, bytes, sizeof(bytes)));
    Check(!ToUtf16(be, 5, Encoding::Utf16BE, units, 256));
    Check(!ToAnsi(be, sizeof(be), static_cast<Encoding>(0), bytes, sizeof(bytes)));
    Check(!ToUtf16(nullptr, 1, Encoding::WindowsAnsi, units, 256));
    Check(!ToAnsi(nullptr, 0, Encoding::WindowsAnsi, bytes, sizeof(bytes)));
    char mac[128];
    for (unsigned i = 0; i < 128; ++i)
        mac[i] = static_cast<char>(i + 128);
    Check(ToAnsi(mac, 128, Encoding::MacRoman, bytes, sizeof(bytes)) == 128);
    // Pins all 128 entries of the pre-refactor, intentionally lossy map.
    Check(Hash(bytes, 128) == 0x90f1dd00u);

    // First-use mapping must be safe under concurrent CLI/engine consumers.
    std::array<std::thread, 8> threads;
    for (auto &thread : threads)
        thread = std::thread([] {
            char original[256], expected[256], actual[256];
            for (unsigned i = 0; i < 256; ++i)
                original[i] = expected[i] = actual[i] = static_cast<char>(i);
            UpperBytes(expected, 256);
            UpperMappedBytes(actual, 256);
            Check(!std::memcmp(expected, actual, 256));
            LowerBytes(original, 256);
            for (unsigned i = 0; i < 256; ++i)
                actual[i] = static_cast<char>(i);
            LowerMappedBytes(actual, 256);
            Check(!std::memcmp(original, actual, 256));
        });
    for (auto &thread : threads)
        thread.join();
    Check(CompareBytes("Alpha", 5, "aLPHA", 5, true) == 0);
    Check(CompareBytes("abc", 3, "abd", 3, false) == -1);
    Check(CompareBytes("", 0, "x", 1, true) == -1);
    Check(CompareBytes("", 0, "", 0, true) == 0);
    std::uint16_t wide[] = {'a', 0xe9, 0};
    UpperUtf16(wide, 2);
    Check(wide[0] == 'A' && wide[1] == 0xc9);
    LowerUtf16(wide, 2);
    Check(wide[0] == 'a' && wide[1] == 0xe9);
    Check(CompareUtf16(wide, 2, wide, 2, false) == 0);
#ifdef NATIVE_ORACLE
    char all[256], native[256], mapped[256];
    for (unsigned i = 0; i < 256; ++i)
        all[i] = native[i] = mapped[i] = static_cast<char>(i);
    CharUpperBuffA(native, 256);
    UpperMappedBytes(mapped, 256);
    Check(!std::memcmp(native, mapped, 256));
    wchar_t nativeWide[512];
    const DWORD flags = GetACP() == CP_UTF8 ? 0 : MB_PRECOMPOSED;
    int count = MultiByteToWideChar(CP_ACP, flags, all, 256, nativeWide, 512);
    auto converted = ToUtf16(all, 256, Encoding::WindowsAnsi, units, 256);
    Check(converted == static_cast<unsigned>(count) && !std::memcmp(units, nativeWide, count * 2));
    count = MultiByteToWideChar(CP_MACCP, MB_PRECOMPOSED, mac, 128, nativeWide, 512);
    converted = ToUtf16(mac, 128, Encoding::MacRoman, units, 256);
    Check(converted == static_cast<unsigned>(count) && !std::memcmp(units, nativeWide, count * 2));
    const wchar_t sample[] = {L'A', 0xe9, 0x20ac, 0x2019, 0x416, 0xd83d, 0xde00, 0, L'Z'};
    unsigned char big[sizeof(sample)];
    for (unsigned i = 0; i < sizeof(sample) / sizeof(sample[0]); ++i)
    {
        big[i * 2] = static_cast<unsigned char>(sample[i] >> 8);
        big[i * 2 + 1] = static_cast<unsigned char>(sample[i]);
    }
    char expectedBytes[128];
    int expectedCount = WideCharToMultiByte(CP_ACP, 0, sample, sizeof(sample) / sizeof(sample[0]), expectedBytes,
                                            sizeof(expectedBytes), nullptr, nullptr);
    Check(ToAnsi(big, sizeof(big), Encoding::Utf16BE, nullptr, 0) == static_cast<unsigned>(expectedCount));
    Check(ToAnsi(big, sizeof(big), Encoding::Utf16BE, bytes, sizeof(bytes)) == static_cast<unsigned>(expectedCount));
    Check(!std::memcmp(bytes, expectedBytes, expectedCount));
    for (const char *left : {"a", "z", "10", "A-a", "e", "\xe9"})
        for (const char *right : {"b", "Z", "2", "Aa", "e", "\xe9"})
        {
            int nativeResult = CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE, left, -1, right, -1);
            int expected = nativeResult ? nativeResult - CSTR_EQUAL : 2;
            Check(CompareBytes(left, std::strlen(left), right, std::strlen(right), true) == expected);
        }
#else
    unsigned char all[256];
    for (unsigned i = 0; i < 256; ++i)
        all[i] = static_cast<unsigned char>(i);
    Check(ToUtf16(all, 256, Encoding::WindowsAnsi, units, 256) == 256);
    Check(units[0x80] == 0x20ac && units[0x9f] == 0x178 && units[0xe9] == 0xe9);
    unsigned char little[512];
    for (unsigned i = 0; i < 256; ++i)
    {
        little[i * 2] = units[i] & 255;
        little[i * 2 + 1] = units[i] >> 8;
    }
    Check(ToAnsi(little, 512, Encoding::Utf16LE, bytes, sizeof(bytes)) == 256 && !std::memcmp(all, bytes, 256));
    Check(ToUtf16(mac, 128, Encoding::MacRoman, units, 256) == 128);
    Check(units[0] == 0xc4 && units[14] == 0xe9);
    const unsigned char emoji[] = {0x3d, 0xd8, 0x00, 0xde};
    Check(ToAnsi(emoji, sizeof(emoji), Encoding::Utf16LE, bytes, sizeof(bytes)) == 1 && bytes[0] == '?');
    char accent[] = {static_cast<char>(0xe9)};
    UpperBytes(accent, 1);
    Check(static_cast<unsigned char>(accent[0]) == 0xc9);
#endif
    return 0;
}
