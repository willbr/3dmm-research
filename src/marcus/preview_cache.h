#ifndef MARCUS_PREVIEW_CACHE_H
#define MARCUS_PREVIEW_CACHE_H
#include "browser_preview.h"
#include <filesystem>

// Successful previews only. Source identity includes paths, sizes and
// modification times; include the decoder executable to invalidate after builds.
class MarcusPreviewCache
{
public:
    MarcusPreviewCache() = default;
    MarcusPreviewCache(const std::filesystem::path &directory, const std::vector<std::filesystem::path> &sources,
                       std::size_t fileLimit = 0);
    const std::string &Key() const { return key; }
    bool Load(unsigned long chunk, BrowserPreview &preview) const;
    void Store(unsigned long chunk, const BrowserPreview &preview) const;
private:
    std::filesystem::path File(unsigned long chunk) const;
    void Trim(const std::filesystem::path &newest) const;
    std::filesystem::path directory;
    std::string key;
    std::size_t fileLimit = 0;
    bool available = false;
};
#endif
