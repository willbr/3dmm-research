/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_content.h"
#include <utility>
namespace StudioContent
{
Root ValidateRoot(Text path, bool windows)
{
    if (path.empty())
        return {};
    const auto separator = [](Char ch) { return ch == '/' || ch == '\\'; };
    const auto drive = [](Char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); };
    const bool absolute = windows ? ((path.length() >= 3 && drive(path[0]) && path[1] == ':' && separator(path[2])) ||
                                     (path.length() >= 3 && separator(path[0]) && separator(path[1])))
                                  : path[0] == '/';
    if (!absolute || path.length() > 255 || path.find(Char(0)) != Text::npos)
        return {Status::Invalid, {}, 0};
    return {Status::Ready, std::move(path), 0};
}
Products ParseProducts(const std::string &bytes)
{
    Products result;
    if (bytes.length() > 1024 * 1024 || bytes.find('\0') != std::string::npos)
        return {Status::Invalid, {}, 0};
    try
    {
        for (std::size_t first = 0; first < bytes.length();)
        {
            auto end = bytes.find('\n', first);
            if (end == std::string::npos)
                end = bytes.length();
            auto line = bytes.substr(first, end - first);
            first = end + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            const auto tab = line.find('\t');
            if (tab == std::string::npos || tab == 0 || tab > 255 || tab + 1 == line.length() ||
                line.length() - tab - 1 > 255 || line.find('\t', tab + 1) != std::string::npos ||
                line.find('\r') != std::string::npos)
                return {Status::Invalid, {}, 0};
            const auto field = [](const std::string &text) {
                Text value;
                for (unsigned char ch : text)
                    value += Char(ch);
                return value;
            };
            result.entries.push_back({field(line.substr(0, tab)), field(line.substr(tab + 1))});
        }
        return result;
    }
    catch (...)
    {
        return {Status::Error, {}, 0};
    }
}
} // namespace StudioContent
