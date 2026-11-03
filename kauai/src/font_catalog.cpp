/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_catalog.h"
#include <algorithm>
#include <limits>
#include <map>
namespace KauaiFonts
{
namespace
{
struct File
{
    KauaiFiles::FileHandle value;
    ~File()
    {
        KauaiFiles::Close(value);
    }
};
struct Directory
{
    KauaiFiles::DirectoryHandle value = nullptr;
    ~Directory()
    {
        if (value)
            KauaiFiles::CloseDirectory(value);
    }
};
bool Read(const FontPath &path, std::vector<std::uint8_t> &bytes)
{
    KauaiFiles::OpenOptions options;
    options.denyWrite = false;
    File file = {KauaiFiles::Open(path.c_str(), options)};
    std::uint32_t size = 0;
    if (!file.value || !KauaiFiles::Size(file.value, &size) || !size || size > INT32_MAX)
        return false;
    bytes.resize(size);
    return KauaiFiles::Read(file.value, bytes.data(), size);
}
std::string Fold(std::string text)
{
    // Font family identifiers are UTF-8; only ASCII case is folded. Never
    // reinterpret UTF-8 bytes through the host's ANSI code page.
    for (auto &ch : text)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return text;
}
bool IsFont(const FontPath &path)
{
    auto dot = path.find_last_of(static_cast<KauaiFiles::PathChar>('.'));
    if (dot == FontPath::npos)
        return false;
    std::string extension;
    for (auto i = dot; i < path.size(); ++i)
    {
        if (path[i] > 127 || path[i] < 0)
            return false;
        extension.push_back(static_cast<char>(path[i]));
    }
    extension = Fold(extension);
    return extension == ".ttf" || extension == ".otf" || extension == ".ttc" || extension == ".otc" ||
           extension == ".fon" || extension == ".fnt" || extension == ".dfont";
}
} // namespace
bool FontCatalog::AddFile(const FontPath &path)
{
    try
    {
        std::vector<std::uint8_t> bytes;
        std::vector<FontInfo> faces;
        if (!Read(path, bytes) || !InspectFreeTypeFont(bytes, faces))
            return false;
        std::vector<FontSource> additions;
        for (const auto &face : faces)
        {
            const auto duplicate = std::find_if(sources.begin(), sources.end(), [&](const FontSource &source) {
                return source.path == path && source.info.faceIndex == face.faceIndex;
            });
            if (duplicate == sources.end())
                additions.push_back({path, face});
        }
        sources.reserve(sources.size() + additions.size());
        for (auto &source : additions)
            sources.push_back(std::move(source));
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool FontCatalog::AddDirectory(const FontPath &path)
{
    return AddDirectoryDepth(path, 0);
}
bool FontCatalog::AddTree(const FontPath &path, unsigned maximumDepth)
{
    return maximumDepth <= 32 && AddDirectoryDepth(path, maximumDepth);
}
bool FontCatalog::AddDirectoryDepth(const FontPath &path, unsigned depth)
{
    try
    {
        FontPath prefix = path;
        if (prefix.empty())
            return false;
        if (!KauaiFiles::IsSeparator(prefix.back()))
            prefix += KauaiFiles::Separator();
        Directory directory;
        KauaiFiles::DirectoryEntry entry;
        auto state =
            KauaiFiles::First((prefix + static_cast<KauaiFiles::PathChar>('*')).c_str(), &directory.value, &entry);
        std::vector<FontPath> paths, directories;
        for (; state == KauaiFiles::Iteration::Entry; state = KauaiFiles::Next(directory.value, &entry))
        {
            if (entry.info.link ||
                (entry.name[0] == '.' && (!entry.name[1] || (entry.name[1] == '.' && !entry.name[2]))))
                continue;
            if (entry.info.directory)
            {
                if (depth)
                    directories.push_back(prefix + entry.name);
            }
            else if (IsFont(entry.name))
                paths.push_back(prefix + entry.name);
        }
        if (state == KauaiFiles::Iteration::Error)
            return false;
        std::sort(paths.begin(), paths.end());
        for (const auto &file : paths)
            AddFile(file);
        std::sort(directories.begin(), directories.end());
        bool complete = true;
        for (const auto &directory : directories)
            complete = AddDirectoryDepth(directory, depth - 1) && complete;
        return complete;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
const FontSource *FontCatalog::Find(const std::string &family, const FreeTypeOptions &options) const
{
    if (family.empty() || options.pixels < 1 || options.pixels > 32767)
        return nullptr;
    const auto key = Fold(family);
    const FontSource *best = nullptr;
    std::int64_t bestScore = std::numeric_limits<std::int64_t>::max();
    for (const auto &source : sources)
    {
        if (Fold(source.info.family) != key)
            continue;
        // A real requested style wins over synthesis. Do not choose a bold or
        // italic face for a normal request merely because it was scanned first.
        const auto style = (source.info.bold == options.bold ? 0
                            : source.info.bold               ? 4
                                                             : 1) +
                           (source.info.italic == options.italic ? 0
                            : source.info.italic                 ? 4
                                                                 : 1);
        const auto size = source.info.bitmapPixels ? std::abs(source.info.bitmapPixels - options.pixels) : 0;
        const auto score = std::int64_t(style) * 65536 + size;
        if (score < bestScore)
        {
            best = &source;
            bestScore = score;
        }
    }
    return best;
}
std::vector<FamilyInfo> FontCatalog::Families(bool includeBitmaps) const
{
    std::map<std::string, const FontSource *> families;
    for (const auto &source : sources)
    {
        if (!includeBitmaps && source.info.bitmapPixels)
            continue;
        const auto key = Fold(source.info.family);
        auto found = families.find(key);
        if (found == families.end())
            families.emplace(key, &source);
        else if (int(source.info.bold) + int(source.info.italic) <
                 int(found->second->info.bold) + int(found->second->info.italic))
            found->second = &source;
    }
    std::vector<FamilyInfo> result;
    result.reserve(families.size());
    for (const auto &family : families)
        result.push_back({family.second->info.family, family.second->info.fixedPitch});
    return result;
}
std::unique_ptr<Face> FontCatalog::Open(const std::string &family, FreeTypeOptions options) const
{
    try
    {
        const auto source = Find(family, options);
        std::vector<std::uint8_t> bytes;
        if (!source || !Read(source->path, bytes))
            return nullptr;
        options.faceIndex = source->info.faceIndex;
        if (options.legacyCellMetrics && source->info.bitmapPixels > 0)
            options.bitmapScale =
                std::max(1, (options.pixels + source->info.bitmapPixels / 2) / source->info.bitmapPixels);
        return CreateFreeTypeFace(std::move(bytes), options);
    }
    catch (const std::bad_alloc &)
    {
        return nullptr;
    }
}
} // namespace KauaiFonts
