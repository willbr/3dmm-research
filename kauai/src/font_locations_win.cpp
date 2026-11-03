/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_locations.h"
#define NOMINMAX
#include <windows.h>
namespace KauaiFonts
{
namespace
{
FontPath Append(FontPath path, const char *suffix)
{
    for (; *suffix; ++suffix)
        path += static_cast<KauaiFiles::PathChar>(*suffix);
    return path;
}
FontPath Environment(const char *name)
{
    KauaiFiles::PathChar value[32768] = {};
#ifdef UNICODE
    std::wstring key;
    for (; *name; ++name)
        key += static_cast<wchar_t>(*name);
    const auto length = GetEnvironmentVariableW(key.c_str(), reinterpret_cast<wchar_t *>(value), 32768);
#else
    const auto length = GetEnvironmentVariableA(name, value, 32768);
#endif
    return length && length < 32768 ? FontPath(value) : FontPath();
}
} // namespace
FontLocations DefaultFontLocations()
{
    FontLocations result;
    const auto additional = Environment("KAUAI_FONT_PATH");
    for (std::size_t start = 0; start < additional.size();)
    {
        auto end = additional.find(static_cast<KauaiFiles::PathChar>(';'), start);
        if (end == FontPath::npos)
            end = additional.size();
        if (end > start)
            result.roots.push_back({additional.substr(start, end - start), true});
        start = end + 1;
    }
    const auto local = Environment("LOCALAPPDATA");
    if (!local.empty())
        result.roots.push_back({Append(local, "\\Microsoft\\Windows\\Fonts"), false});
    KauaiFiles::PathChar windows[32768] = {};
#ifdef UNICODE
    const auto length = GetWindowsDirectoryW(reinterpret_cast<wchar_t *>(windows), 32768);
#else
    const auto length = GetWindowsDirectoryA(windows, 32768);
#endif
    if (length && length < 32760)
    {
        result.roots.push_back({Append(windows, "\\Fonts"), false});
        result.systemBitmap = Append(windows, "\\Fonts\\vgasys.fon");
    }
    return result;
}
} // namespace KauaiFonts
