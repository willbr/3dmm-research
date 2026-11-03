/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "text_utf8.h"
namespace KauaiText
{
bool EncodeUtf8(const std::vector<std::uint16_t> &units, std::string &result)
{
    result.clear();
    for (std::size_t i = 0; i < units.size(); ++i)
    {
        std::uint32_t ch = units[i];
        if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF)
            ch = 0x10000 + ((ch - 0xD800) << 10) + units[++i] - 0xDC00;
        else if (ch >= 0xD800 && ch <= 0xDFFF)
            return false;
        if (ch < 0x80)
            result += static_cast<char>(ch);
        else
        {
            if (ch >= 0x10000)
                result += static_cast<char>(0xF0 | (ch >> 18));
            if (ch >= 0x800)
                result += static_cast<char>((ch >= 0x10000 ? 0x80 : 0xE0) | ((ch >> 12) & 0x3F));
            result += static_cast<char>((ch >= 0x800 ? 0x80 : 0xC0) | ((ch >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    return true;
}
bool DecodeUtf8(const std::string &name, std::vector<std::uint16_t> &units)
{
    units.clear();
    for (std::size_t i = 0; i < name.size();)
    {
        const auto first = static_cast<unsigned char>(name[i++]);
        unsigned count = 0;
        std::uint32_t ch = first;
        if (first >= 0xC2 && first <= 0xDF)
        {
            count = 1;
            ch &= 0x1F;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            count = 2;
            ch &= 0x0F;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            count = 3;
            ch &= 0x07;
        }
        else if (first >= 0x80)
            return false;
        if (count > name.size() - i)
            return false;
        for (unsigned j = 0; j < count; ++j)
        {
            const auto next = static_cast<unsigned char>(name[i++]);
            if ((next & 0xC0) != 0x80)
                return false;
            ch = (ch << 6) | (next & 0x3F);
        }
        if ((count == 1 && ch < 0x80) || (count == 2 && ch < 0x800) || (count == 3 && ch < 0x10000) || ch > 0x10FFFF ||
            (ch >= 0xD800 && ch <= 0xDFFF))
            return false;
        if (ch >= 0x10000)
        {
            units.push_back(static_cast<std::uint16_t>(0xD800 + ((ch - 0x10000) >> 10)));
            units.push_back(static_cast<std::uint16_t>(0xDC00 + ((ch - 0x10000) & 0x3FF)));
        }
        else
            units.push_back(static_cast<std::uint16_t>(ch));
    }
    return true;
}
} // namespace KauaiText
