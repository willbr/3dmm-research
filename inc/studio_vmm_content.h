// Licensed under the MIT License.
#ifndef STUDIO_VMM_CONTENT_H
#define STUDIO_VMM_CONTENT_H
#include "studio_vxp_store.h"
#include <functional>

namespace StudioV3dmm
{
struct MoviePackages
{
    std::vector<Installation> portable;
    std::vector<std::string> libraries;
};

// A private materializer leaves the installed catalog unchanged. The catalog
// provider is called lazily when the movie has an embedded dependency.
using PrivatePackage = std::function<Installation(const MM::V3dmm::Dependency &, std::size_t,
                                                  const MM::V3dmm::Package &)>;
MoviePackages ResolveMoviePackages(const MM::V3dmm::Bundle &,
                                  const std::function<bool(const std::string &)> &libraryAvailable,
                                  const std::function<Store *()> &openCatalog,
                                  const PrivatePackage &materialize = {});
} // namespace StudioV3dmm
#endif
