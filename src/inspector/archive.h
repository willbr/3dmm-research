/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef INSPECTOR_ARCHIVE_H
#define INSPECTOR_ARCHIVE_H
#include "three_mm/archive.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Inspector
{
using Id = ThreeMM::Id;
using Child = ThreeMM::Child;
using Chunk = ThreeMM::Chunk;
using StringTableEntry = ThreeMM::StringTableEntry;
struct StringTablePage
{
    bool present = false;
    std::uint32_t totalEntries = 0, firstEntry = 0, extraBytes = 0;
    std::string error;
    std::vector<StringTableEntry> entries;
};
struct Detail
{
    std::uint32_t totalBytes = 0, offset = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<std::string> fields;
    StringTablePage strings;
};
// UI-neutral, read-only archive session. Paths and labels at this boundary are UTF-8.
// A failed Open retains the previous archive. Payload reads are paged (4096 bytes).
class Archive
{
    struct Impl;
    std::unique_ptr<Impl> _impl;

  public:
    Archive();
    ~Archive();
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;
    bool Open(const std::string &utf8Path, std::string &error);
    void Close();
    const std::vector<Chunk> &Chunks() const;
    const std::string &Path() const;
    bool ReadDetail(std::size_t index, std::uint32_t offset, Detail &detail, std::string &error,
                    std::uint32_t firstString = 0);
};
using ThreeMM::Identity;
using ThreeMM::TypeDisplayName;
using ThreeMM::TypeLabel;
} // namespace Inspector
#endif
