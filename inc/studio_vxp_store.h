// Licensed under the MIT License.
#ifndef STUDIO_VXP_STORE_H
#define STUDIO_VXP_STORE_H
#include "three_mm/v3dmm.h"
namespace StudioV3dmm
{
struct Installation
{
    std::string name, author, hash;
    bool enabled = true;
    std::filesystem::path archive;
    std::map<std::string, std::filesystem::path> members;
};
class Store
{
    std::filesystem::path root;
    std::vector<Installation> entries;
    Installation Read(const std::string &hash, bool enabled) const;
    void Save(const std::vector<Installation> &next);

  public:
    explicit Store(std::filesystem::path directory);
    const std::vector<Installation> &Entries() const
    {
        return entries;
    }
    const std::filesystem::path &Root() const
    {
        return root;
    }
    Installation Install(const ThreeMM::V3dmm::Range &source);
    void SetEnabled(const std::string &hash, bool enabled);
};
} // namespace StudioV3dmm
#endif
