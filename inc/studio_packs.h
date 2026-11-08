/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PACKS_H
#define STUDIO_PACKS_H
#include <array>
#include <filesystem>
#include <string>
#include <vector>
namespace StudioPacks
{
enum class Id
{
    Nickelodeon,
    Doraemon,
    Frankie
};
struct Definition
{
    Id id;
    const char *key, *title, *sourceTitle, *sourceFolder;
    int sourceId;
    std::vector<std::string> files;
};
const std::array<Definition, 3> &Definitions();
int Identify(const std::string &sourceName);
struct Preference
{
    std::string generation;
    bool enabled = true, remove = false;
    int sourceId = 0;
};
using Preferences = std::array<Preference, 3>;
std::string Serialize(const Preferences &preferences);
Preferences Parse(const std::string &text);
struct Info
{
    bool installed = false, valid = false;
    std::string path, error;
};
struct Staged
{
    Id id = Id::Nickelodeon;
    std::string generation;
    std::filesystem::path path;
};
class Store
{
    std::filesystem::path root;

  public:
    explicit Store(std::filesystem::path directory);
    const std::filesystem::path &Root() const
    {
        return root;
    }
    Info Inspect(Id id, const Preference &preference, bool hashes = false) const;
    Staged StageFolder(Id id, const std::filesystem::path &source);
    Staged StageISO(Id id, const std::filesystem::path &iso, const std::filesystem::path &baseISO,
                    const std::filesystem::path &fixups);
    void Publish(Staged &stage);
    void Discard(Staged &stage);
    // No recursive deletion: remove only recorded, unchanged files. Unknown or
    // modified files survive in the inactive generation. Missing files are OK.
    unsigned Remove(Id id, const Preference &preference);
};
// Native ISO import, shared by the manager and isolated tests. output is empty
// staging owned by the caller. Nickelodeon requires the original base ISO/fixups.
void ImportISO(Id id, const std::filesystem::path &iso, const std::filesystem::path &baseISO,
               const std::filesystem::path &fixups, const std::filesystem::path &output);
} // namespace StudioPacks
#endif
