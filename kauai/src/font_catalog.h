/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_FONT_CATALOG_H
#define KAUAI_FONT_CATALOG_H
#include "font_freetype.h"
#include "platform_files.h"
#include <string>
namespace KauaiFonts
{
using FontPath = std::basic_string<KauaiFiles::PathChar>;
struct FontSource
{
    FontPath path;
    FontInfo info;
};
class FontCatalog
{
    std::vector<FontSource> sources;
    bool AddDirectoryDepth(const FontPath &path, unsigned depth);

  public:
    // Reads metadata only. Faces own fresh font bytes when opened, so catalog
    // lifetime is independent of live faces. Invalid files leave it unchanged.
    bool AddFile(const FontPath &path);
    // Nonrecursive; ignores links/directories and non-font files. The host
    // supplies search roots and their precedence. No SDK types or registry.
    bool AddDirectory(const FontPath &path);
    // Bounded traversal for system font trees; never follows links.
    bool AddTree(const FontPath &path, unsigned maximumDepth = 8);
    const std::vector<FontSource> &Sources() const
    {
        return sources;
    }
    const FontSource *Find(const std::string &family, const FreeTypeOptions &options) const;
    // One entry per family; outline faces only unless bitmap families are
    // explicitly requested. Use the normal face's pitch when available.
    std::vector<FamilyInfo> Families(bool includeBitmaps = false) const;
    // An unknown family fails; the caller owns any substitution policy.
    std::unique_ptr<Face> Open(const std::string &family, FreeTypeOptions options) const;
};
} // namespace KauaiFonts
#endif
