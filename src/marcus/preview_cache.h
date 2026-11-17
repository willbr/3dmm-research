#ifndef MARCUS_PREVIEW_CACHE_H
#define MARCUS_PREVIEW_CACHE_H
#include "browser_preview.h"
#include <filesystem>

// Successful background previews only. Source identity includes paths, sizes and
// modification times; include the decoder executable to invalidate after builds.
class MarcusPreviewCache
{
public:
    MarcusPreviewCache() = default;
    MarcusPreviewCache(const std::filesystem::path &directory, const std::vector<std::filesystem::path> &sources);
    const std::string &Key() const { return key; }
    bool Load(unsigned long chunk, BrowserPreview &preview) const;
    void Store(unsigned long chunk, const BrowserPreview &preview) const;
private:
    std::filesystem::path File(unsigned long chunk) const;
    std::filesystem::path directory;
    std::string key;
    bool available = false;
};
#endif
