/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PACK_RUNTIME_H
#define STUDIO_PACK_RUNTIME_H
#include "studio_packs.h"
#include "studio_content.h"
#include <memory>
namespace StudioPacks
{
enum class Access
{
    Legacy,
    Available,
    Disabled,
    Missing
};
void Initialize(const StudioContent::Text &contentRoot);
Store &ManagedStore();
const Preferences &ActivePreferences();
Preferences SavedPreferences();
void SavePreferences(const Preferences &preferences);
StudioContent::Products Products();
Access Resolve(const std::string &longName, const std::string &shortName, std::string &path);
class Edits
{
    Preferences original;
    std::array<Staged, 3> staged;
    std::array<bool, 3> beforeRemoval{};

  public:
    Preferences values;
    Edits();
    ~Edits();
    Info Inspect(std::size_t index) const;
    bool Managed(std::size_t index) const;
    void Install(std::size_t index, const std::filesystem::path &source, const std::filesystem::path &baseISO,
                 const std::filesystem::path &fixups);
    void ToggleRemove(std::size_t index);
    void Commit();
};
} // namespace StudioPacks
#endif
