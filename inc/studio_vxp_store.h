// Licensed under the MIT License.
#ifndef STUDIO_VXP_STORE_H
#define STUDIO_VXP_STORE_H
#include "three_mm/v3dmm.h"
#include <memory>
namespace StudioV3dmm
{
struct Installation
{
    std::string name, author, hash;
    bool enabled = true;
    std::filesystem::path archive;
    std::map<std::string, std::filesystem::path> members;
    std::string shortName, error;
};
class Store
{
    std::filesystem::path root;
    std::vector<Installation> entries;
    struct Catalog;
    std::unique_ptr<Catalog> catalog;

  public:
    explicit Store(std::filesystem::path directory);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    const std::vector<Installation> &Entries() const
    {
        return entries;
    }
    const std::filesystem::path &Root() const
    {
        return root;
    }
    Installation Install(const ThreeMM::V3dmm::Range &source);
    // Metadata listing performs no package I/O. Resolve only applied packages;
    // unchanged, previously verified files reuse their persisted fingerprints.
    Installation Resolve(const std::string &hash, bool forceValidation = false);
    void SetEnabled(const std::string &hash, bool enabled);
};
} // namespace StudioV3dmm
#endif
