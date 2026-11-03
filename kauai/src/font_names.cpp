/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_names.h"
#include "platform_text.h"
#include "text_utf8.h"
#include <cstring>
namespace KauaiFonts
{
bool TextToUtf8(Text name, std::string &utf8)
{
    utf8.clear();
    try
    {
        if ((!name.data && name.units) || name.units > INT32_MAX)
            return false;
        std::vector<std::uint16_t> units;
        if (name.encoding == Encoding::legacyBytes)
        {
            auto count = KauaiText::ToUtf16(name.data, name.units, KauaiText::Encoding::WindowsAnsi, nullptr, 0);
            if (!count && name.units)
                return false;
            units.resize(count);
            if (count &&
                !KauaiText::ToUtf16(name.data, name.units, KauaiText::Encoding::WindowsAnsi, units.data(), count))
                return false;
        }
        else if (name.encoding == Encoding::utf16)
        {
            units.resize(name.units);
            if (name.units)
                std::memcpy(units.data(), name.data, name.units * 2);
        }
        else
            return false;
        std::string result;
        if (!KauaiText::EncodeUtf8(units, result))
            return false;
        utf8.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool NameToUtf8(Text name, std::string &utf8)
{
    if (TextToUtf8(name, utf8) && !utf8.empty() && utf8.find('\0') == std::string::npos)
        return true;
    utf8.clear();
    return false;
}
bool NameFromUtf8(const std::string &utf8, Encoding encoding, std::vector<std::uint8_t> &name)
{
    name.clear();
    try
    {
        if (utf8.size() > INT32_MAX)
            return false;
        std::vector<std::uint16_t> units;
        if (utf8.empty() || utf8.find('\0') != std::string::npos || !KauaiText::DecodeUtf8(utf8, units))
            return false;
        std::vector<std::uint8_t> result;
        if (encoding == Encoding::utf16)
        {
            result.resize(units.size() * 2);
            std::memcpy(result.data(), units.data(), result.size());
        }
        else if (encoding == Encoding::legacyBytes)
        {
            std::vector<std::uint8_t> littleEndian(units.size() * 2);
            for (std::size_t i = 0; i < units.size(); ++i)
            {
                littleEndian[2 * i] = static_cast<std::uint8_t>(units[i]);
                littleEndian[2 * i + 1] = static_cast<std::uint8_t>(units[i] >> 8);
            }
            auto count =
                KauaiText::ToAnsi(littleEndian.data(), littleEndian.size(), KauaiText::Encoding::Utf16LE, nullptr, 0);
            if (!count)
                return false;
            result.resize(count);
            if (!KauaiText::ToAnsi(littleEndian.data(), littleEndian.size(), KauaiText::Encoding::Utf16LE,
                                   reinterpret_cast<char *>(result.data()), count))
                return false;
            const auto roundtrip =
                KauaiText::ToUtf16(result.data(), result.size(), KauaiText::Encoding::WindowsAnsi, nullptr, 0);
            if (roundtrip != units.size())
                return false;
            std::vector<std::uint16_t> restored(roundtrip);
            if (!KauaiText::ToUtf16(result.data(), result.size(), KauaiText::Encoding::WindowsAnsi, restored.data(),
                                    restored.size()) ||
                restored != units)
                return false;
        }
        else
            return false;
        name.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace KauaiFonts
