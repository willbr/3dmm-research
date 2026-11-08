// Licensed under the MIT License.
#include "three_mm/v3dmm.h"
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace ThreeMM::V3dmm
{
void Publish(const std::filesystem::path &candidate, const std::filesystem::path &destination)
{
    if (std::filesystem::weakly_canonical(candidate.parent_path()) !=
        std::filesystem::weakly_canonical(destination.parent_path()))
        throw std::runtime_error("V3dmm publication must stay on the same filesystem");
#ifdef _WIN32
    if (!MoveFileExW(candidate.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish v3dmm file");
#else
    std::filesystem::rename(candidate, destination);
#endif
}
} // namespace ThreeMM::V3dmm
