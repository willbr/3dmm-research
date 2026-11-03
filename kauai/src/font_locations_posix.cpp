/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_locations.h"
#include <cstdlib>
namespace KauaiFonts
{
namespace
{
void Roots(std::vector<FontSearchRoot> &roots, const char *list, const char *suffix)
{
    if (!list)
        return;
    std::string paths(list);
    for (std::size_t start = 0; start < paths.size();)
    {
        auto end = paths.find(':', start);
        if (end == std::string::npos)
            end = paths.size();
        if (end > start)
            roots.push_back({paths.substr(start, end - start) + suffix, true});
        start = end + 1;
    }
}
} // namespace
FontLocations DefaultFontLocations()
{
    FontLocations result;
    Roots(result.roots, std::getenv("KAUAI_FONT_PATH"), "");
    const auto home = std::getenv("HOME");
#ifdef __APPLE__
    if (home && *home)
        result.roots.push_back({std::string(home) + "/Library/Fonts", true});
    result.roots.push_back({"/Library/Fonts", true});
    result.roots.push_back({"/System/Library/Fonts", true});
    result.systemFamilies = {"Helvetica", "Helvetica Neue", "Arial", "DejaVu Sans", "Liberation Sans"};
#else
    const auto data = std::getenv("XDG_DATA_HOME");
    if (data && *data)
        result.roots.push_back({std::string(data) + "/fonts", true});
    else if (home && *home)
        result.roots.push_back({std::string(home) + "/.local/share/fonts", true});
    if (home && *home)
        result.roots.push_back({std::string(home) + "/.fonts", true});
    const auto directories = std::getenv("XDG_DATA_DIRS");
    Roots(result.roots, directories && *directories ? directories : "/usr/local/share:/usr/share", "/fonts");
    result.systemFamilies = {"DejaVu Sans", "Liberation Sans", "Noto Sans", "Arial", "Helvetica"};
#endif
    return result;
}
} // namespace KauaiFonts
