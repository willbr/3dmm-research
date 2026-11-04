/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_host.h"
#include <cstdlib>
#include <cstring>
#ifdef KAUAI_FREETYPE
#include "font_locations.h"
#include "font_names.h"
#include <algorithm>
#endif
namespace KauaiFonts
{
namespace
{
ProviderStats stats; // Studio's UI-thread service, like the graphics ports.
#ifdef KAUAI_FREETYPE
bool IsSystem(std::string name)
{
    for (auto &ch : name)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return name == "system";
}
FileFontHost &StudioFonts()
{
    static FileFontHost host;
    static const bool initialized = host.Initialize(DefaultFontLocations());
    (void)initialized;
    return host;
}
#endif
} // namespace
#ifdef KAUAI_FREETYPE
bool FileFontHost::Initialize(const FontLocations &locations)
{
    try
    {
        FileFontHost replacement;
        for (const auto &root : locations.roots)
            if (root.recursive)
                replacement.fonts.AddTree(root.path);
            else
                replacement.fonts.AddDirectory(root.path);
        if (!locations.systemBitmap.empty())
        {
            if (!replacement.system.AddFile(locations.systemBitmap))
                return false;
            auto families = replacement.system.Families(true);
            if (families.empty())
                return false;
            replacement.systemFamily = families.front().name;
            replacement.bitmapSystem = true;
        }
        else
        {
            FreeTypeOptions request;
            request.legacyCellMetrics = true;
            for (const auto &name : locations.systemFamilies)
            {
                auto face = replacement.fonts.Open(name, request);
                Metrics metrics;
                if (face && face->Measure({"Ag", 2}, metrics) && metrics.cellHeight > 0)
                {
                    replacement.systemFamily = name;
                    break;
                }
            }
            if (replacement.systemFamily.empty())
                return false;
        }
        *this = std::move(replacement);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool FileFontHost::Families(std::vector<FamilyInfo> &families) const
{
    families.clear();
    try
    {
        if (systemFamily.empty())
            return false;
        auto result = fonts.Families();
        result.erase(std::remove_if(result.begin(), result.end(),
                                    [](const FamilyInfo &family) { return IsSystem(family.name); }),
                     result.end());
        FreeTypeOptions request;
        const auto source = (bitmapSystem ? system : fonts).Find(systemFamily, request);
        if (!source)
            return false;
        result.push_back({"System", source->info.fixedPitch});
        families.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
std::unique_ptr<Face> FileFontHost::Open(Text family, std::int32_t pixels, bool bold, bool italic, bool underline) const
{
    try
    {
        std::string name;
        if (!NameToUtf8(family, name))
            return nullptr;
        FreeTypeOptions request;
        request.pixels = pixels;
        request.bold = bold;
        request.italic = italic;
        request.underline = underline;
        request.legacyCellMetrics = true;
        if (IsSystem(name))
            return systemFamily.empty() ? nullptr : (bitmapSystem ? system : fonts).Open(systemFamily, request);
        return fonts.Open(name, request);
    }
    catch (const std::bad_alloc &)
    {
        return nullptr;
    }
}
#endif
const char *StudioFontProvider()
{
#ifdef KAUAI_FREETYPE
#if defined(_WIN32) && !defined(KAUAI_PORTABLE_RUNTIME)
    const auto name = std::getenv("KAUAI_FONT_PROVIDER");
    return name && std::strcmp(name, "gdi") == 0 ? "gdi" : "freetype";
#else
    return "freetype";
#endif
#else
#if defined(_WIN32) && !defined(KAUAI_PORTABLE_RUNTIME)
    return "gdi";
#else
    return "none";
#endif
#endif
}
ProviderStats FontProviderStats()
{
    return stats;
}
void RecordFontFallback(bool measurement)
{
    if (std::strcmp(StudioFontProvider(), "freetype"))
        return;
    if (measurement)
        ++stats.nativeMeasurements;
    else
        ++stats.nativeDraws;
}
void RecordFontEnumeration(bool portable)
{
    if (portable)
        ++stats.portableEnumerations;
    else
        ++stats.nativeEnumerations;
}
void RecordNativeFontBootstrap()
{
    ++stats.nativeBootstraps;
}
void RecordFontFace(bool portable)
{
    if (portable)
        ++stats.portable;
    else
        ++stats.native;
}
void RecordFontCreationFallback()
{
    ++stats.fallback;
}
bool StudioFontFamilies(std::vector<FamilyInfo> &families)
{
    families.clear();
#ifdef KAUAI_FREETYPE
    try
    {
        return StudioFonts().Families(families);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
#else
    return false;
#endif
}
std::unique_ptr<Face> CreateStudioFace(Text family, std::int32_t pixels, bool bold, bool italic, bool underline)
{
#ifdef KAUAI_FREETYPE
    try
    {
        auto face = StudioFonts().Open(family, pixels, bold, italic, underline);
        if (face)
            RecordFontFace(true);
        else
            RecordFontCreationFallback();
        return face;
    }
    catch (const std::bad_alloc &)
    {
        RecordFontCreationFallback();
        return nullptr;
    }
#else
    return nullptr;
#endif
}
} // namespace KauaiFonts
