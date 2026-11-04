/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "file_browser.h"
#include "platform_files.h"
#include "platform_text.h"
#include "text_utf8.h"
#include <algorithm>
#include <cstring>
namespace KauaiFiles
{
namespace
{
using Path = std::basic_string<PathChar>;
bool Native(const std::string &text, Path &path)
{
    if (text.find('\0') != std::string::npos)
        return false;
#if defined(UNICODE) || defined(_WIN32)
    std::vector<std::uint16_t> words;
    if (!KauaiText::DecodeUtf8(text, words))
        return false;
#ifdef UNICODE
    path.assign(words.begin(), words.end());
#else
    const std::uint16_t endian = 1;
    const auto encoding =
        *reinterpret_cast<const unsigned char *>(&endian) ? KauaiText::Encoding::Utf16LE : KauaiText::Encoding::Utf16BE;
    const auto count = KauaiText::ToAnsi(words.data(), words.size() * 2, encoding, nullptr, 0);
    path.resize(count);
    if (!words.empty() && (!count || !KauaiText::ToAnsi(words.data(), words.size() * 2, encoding, &path[0], count)))
        return false;
#endif
#else
    path = text;
#endif
    return true;
}
std::string Utf8(const PathChar *path)
{
#ifdef UNICODE
    std::vector<std::uint16_t> words(path, path + std::char_traits<PathChar>::length(path));
#elif defined(_WIN32)
    const auto bytes = std::strlen(path);
    const auto count = KauaiText::ToUtf16(path, bytes, KauaiText::Encoding::WindowsAnsi, nullptr, 0);
    std::vector<std::uint16_t> words(count);
    if (bytes && (!count || !KauaiText::ToUtf16(path, bytes, KauaiText::Encoding::WindowsAnsi, words.data(), count)))
        return {};
#else
    return path;
#endif
#if defined(UNICODE) || defined(_WIN32)
    std::string result;
    return KauaiText::EncodeUtf8(words, result) ? result : std::string();
#endif
}
bool Resolve(const std::string &text, Path &native, std::string &full)
{
    Path input;
    PathChar buffer[4096];
    if (!Native(text, input) || !FullPath(input.c_str(), buffer, 4096))
        return false;
    native = buffer;
    full = Utf8(buffer);
    // Reject lossy native conversion rather than choosing a different file.
    return !full.empty() && Utf8(input.c_str()) == text;
}
std::string Fold(std::string value)
{
    for (auto &ch : value)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return value;
}
bool Absolute(const std::string &path)
{
    return !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
}
std::string Join(const std::string &directory, const std::string &leaf)
{
    return directory + (directory.empty() || IsSeparator(static_cast<PathChar>(directory.back())) ? "" : "/") + leaf;
}
} // namespace
bool NativePathFromUtf8(const std::string &text, std::basic_string<PathChar> &path)
{
    return Native(text, path);
}
bool Browser::Matches(const std::string &name, const std::string &patterns)
{
    const auto text = Fold(name);
    std::size_t begin = 0;
    do
    {
        auto end = patterns.find(';', begin);
        auto pattern = Fold(patterns.substr(begin, end == std::string::npos ? end : end - begin));
        if (pattern == "*.*")
            pattern = "*";
        std::size_t i = 0, j = 0, star = std::string::npos, retry = 0;
        while (i < text.size())
        {
            if (j < pattern.size() && (pattern[j] == '?' || pattern[j] == text[i]))
            {
                ++i;
                ++j;
            }
            else if (j < pattern.size() && pattern[j] == '*')
            {
                star = j++;
                retry = i;
            }
            else if (star != std::string::npos)
            {
                j = star + 1;
                i = ++retry;
            }
            else
                break;
        }
        while (j < pattern.size() && pattern[j] == '*')
            ++j;
        if (i == text.size() && j == pattern.size())
            return true;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    } while (begin <= patterns.size());
    return false;
}
bool Browser::Navigate(const std::string &path)
{
    Path native;
    std::string full;
    PathInfo info;
    if (!Resolve(path, native, full) || Info(native.c_str(), &info) != Status::Found || !info.directory)
    {
        error = "This folder is unavailable.";
        return false;
    }
    auto previous = directory;
    directory = full;
    if (!Refresh())
    {
        directory = previous;
        return false;
    }
    selection.clear();
    if (open)
        name.clear();
    return true;
}
bool Browser::Refresh()
{
    Path pattern;
    if (!Native(Join(directory, "*"), pattern))
        return false;
    DirectoryEntry entry;
    DirectoryHandle handle = nullptr;
    auto state = First(pattern.c_str(), &handle, &entry);
    struct Close
    {
        DirectoryHandle &handle;
        ~Close()
        {
            if (handle)
                CloseDirectory(handle);
        }
    } close{handle};
    std::vector<BrowserEntry> loaded;
    while (state == Iteration::Entry)
    {
        auto name = Utf8(entry.name);
        if (!name.empty() && name != "." && name != ".." && !entry.info.hidden && !entry.info.system &&
            (entry.info.directory || filters.empty() || filter >= filters.size() ||
             Matches(name, filters[filter].patterns)))
            loaded.push_back({name, Join(directory, name), entry.info.directory});
        state = Next(handle, &entry);
    }
    if (state == Iteration::Error)
    {
        error = "This folder could not be read.";
        return false;
    }
    std::sort(loaded.begin(), loaded.end(), [](const BrowserEntry &a, const BrowserEntry &b) {
        if (a.directory != b.directory)
            return a.directory;
        const auto x = Fold(a.name), y = Fold(b.name);
        return x == y ? a.name < b.name : x < y;
    });
    entries.swap(loaded);
    error.clear();
    return true;
}
bool Browser::Up()
{
    return Navigate(Join(directory, ".."));
}
void Browser::Select(unsigned index)
{
    if (index >= entries.size())
        return;
    const auto &entry = entries[index];
    selection = entry.directory ? std::string() : entry.path;
    if (!entry.directory || open)
        name = entry.name;
    error.clear();
}
bool Browser::Accept()
{
    if (name.empty())
    {
        error = "Choose a file name.";
        return false;
    }
    auto path = Absolute(name) ? name : Join(directory, name);
    Path native;
    std::string full;
    PathInfo info;
    if (!Resolve(path, native, full))
    {
        error = "This file name cannot be used.";
        return false;
    }
    auto status = Info(native.c_str(), &info);
    if (status == Status::Found && info.directory)
    {
        Navigate(full);
        return false;
    }
    if (!open && !defaultExtension.empty())
    {
        const auto slash = path.find_last_of("/\\"), dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash) ||
            Fold(path.substr(dot + 1)) != Fold(defaultExtension))
            path += "." + defaultExtension;
        if (!Resolve(path, native, full))
        {
            error = "This file name cannot be used.";
            return false;
        }
        status = Info(native.c_str(), &info);
    }
    if (status == Status::Error || (open && status != Status::Found) || (status == Status::Found && info.directory))
    {
        error = open ? "This file is unavailable." : "This file name cannot be used.";
        return false;
    }
    const auto slash = full.find_last_of("/\\");
    Path parent;
    if (slash == std::string::npos || !Native(full.substr(0, slash + 1), parent) ||
        Info(parent.c_str(), &info) != Status::Found || !info.directory)
    {
        error = "The destination folder is unavailable.";
        return false;
    }
    selection = full;
    error.clear();
    return true;
}
} // namespace KauaiFiles
