// Licensed under the MIT License.
#ifndef STUDIO_V3DMM_H
#define STUDIO_V3DMM_H
#pragma push_macro("size")
#undef size
#include <string>
#include <vector>
#pragma pop_macro("size")
namespace StudioV3dmm
{
class Store;
void Initialize();
Store &Packages();
std::vector<std::string> AppliedPackages(PMovie movie);
void SetAppliedPackages(PMovie movie, const std::vector<std::string> &hashes);
bool Resolve(PString longName, PString shortName, PFilename path);
bool Archive(LegacyLong sid, FileType type, LegacyLong index, PFilename path);
std::string Display(const std::string &legacyBytes);
} // namespace StudioV3dmm
#endif
