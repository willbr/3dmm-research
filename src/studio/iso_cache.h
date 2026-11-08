/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_ISO_CACHE_H
#define STUDIO_ISO_CACHE_H
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
namespace StudioISO
{
struct Supplement
{
    std::string name;
    const unsigned char *data;
    std::size_t bytes;
};
struct Cache
{
    std::filesystem::path root;
    bool reused = false;
};
// Read-only ISO 9660 input. Only the retail product, samples, help and fonts
// are extracted. Cache entries are published atomically after all writes finish.
// Invalid or incomplete existing entries are preserved and a new entry is used.
// Throws std::exception with a diagnostic on unsupported/corrupt input or I/O.
Cache Prepare(const std::filesystem::path &image, const std::filesystem::path &cacheDirectory,
              const std::vector<Supplement> &supplements = {});
std::map<std::string, std::vector<unsigned char>> ReadFiles(const std::filesystem::path &image,
                                                            const std::string &directory,
                                                            const std::vector<std::string> &names);
} // namespace StudioISO
#endif
