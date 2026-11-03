/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_FONT_LOCATIONS_H
#define KAUAI_FONT_LOCATIONS_H
#include "font_catalog.h"
namespace KauaiFonts
{
struct FontSearchRoot
{
    FontPath path;
    bool recursive = false;
};
struct FontLocations
{
    std::vector<FontSearchRoot> roots;       // Highest precedence first.
    FontPath systemBitmap;                   // Optional exact legacy System bitmap resource.
    std::vector<std::string> systemFamilies; // Ordered fallback if no bitmap is specified.
};
// File-based discovery/policy requires the KAUAI_FREETYPE build option.
FontLocations DefaultFontLocations();
// SDK-free policy, independently usable without the global Studio instance.
class FileFontHost
{
    FontCatalog fonts, system;
    std::string systemFamily;
    bool bitmapSystem = false;

  public:
    // Transactional: a failed discovery leaves an existing host usable.
    bool Initialize(const FontLocations &locations);
    bool Families(std::vector<FamilyInfo> &families) const;
    std::unique_ptr<Face> Open(Text family, std::int32_t pixels, bool bold, bool italic, bool underline) const;
};
} // namespace KauaiFonts
#endif
