// Licensed under the MIT License.
#ifndef STUDIO_VMM_CONTENT_H
#define STUDIO_VMM_CONTENT_H
#include "studio_vxp_store.h"
#include "mm/content_vfs.hpp"
#include <functional>

namespace StudioV3dmm
{
struct MoviePackages
{
    std::vector<Installation> portable;
    std::vector<std::string> libraries;
};

// Build the active-catalog VMM declarations and open only selected archives.
std::vector<MM::V3dmm::Dependency> ActiveMovieDependencies(const std::vector<std::string> &libraries,
                                                        const std::vector<Installation> &portable);

// A private materializer leaves the installed catalog unchanged. The catalog
// provider is called lazily when the movie has an embedded dependency.
using PrivatePackage = std::function<Installation(const MM::V3dmm::Dependency &, std::size_t,
                                                  const MM::V3dmm::Package &)>;
MoviePackages ResolveMoviePackages(const MM::V3dmm::Bundle &,
                                  const std::function<bool(const std::string &)> &libraryAvailable,
                                  const std::function<Store *()> &openCatalog,
                                  const PrivatePackage &materialize = {});

// Studio supplies its native filename check. Marcus uses filesystem paths.
void MountMoviePackages(MM::Content::Vfs &, const std::vector<Installation> &,
                        const std::function<void(const std::filesystem::path &)> &validatePath = {});
} // namespace StudioV3dmm
#endif
