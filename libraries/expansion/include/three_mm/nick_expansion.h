/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_NICK_EXPANSION_H
#define THREE_MM_NICK_EXPANSION_H
#include <cstddef>
#include <string>

namespace ThreeMM
{
struct NickConversionStats
{
    std::size_t archives = 0, chunks = 0, images = 0, palettes = 0;
};
// Paths use the legacy core's native narrow path repertoire and 255-byte limit.
// source contains original NICK3D disc files, including NICK3D.CHK. baseResources
// is the base 3DMM disc's 3DMOVIE.CHK. output must be an existing empty directory;
// on failure the caller owns cleanup. No reference pack/profile is read.
// Produces an intermediate candidate using deterministic nearest-colour image
// conversion. Historical pixel equivalence requires local image fixups, applied
// by the host through ReplaceNickImages and verified after saving.
bool ConvertNickExpansion(const std::string &source, const std::string &baseResources, const std::string &output,
                          NickConversionStats &stats, std::string &error);
struct NickComparison
{
    std::size_t archives = 0, chunks = 0, imageDifferences = 0, otherDifferences = 0;
    std::string firstDifference;
};
// Physical offsets/compression are ignored; IDs, links, flags, names and decoded
// data are compared. Image/palette differences are reported separately.
bool CompareNickExpansion(const std::string &actual, const std::string &reference, NickComparison &comparison,
                          std::string &error);
// Local fixup tooling exports/replaces only decoded MBMP, TMAP and GLCR data.
// File names are generated from archive index and chunk identity. Chunk IDs,
// names, flags, links, models and all other payloads remain unchanged.
bool ExportNickImages(const std::string &source, const std::string &output, std::string &error);
bool ReplaceNickImages(const std::string &source, const std::string &images, const std::string &output,
                       std::string &error);
// Canonical metadata and non-image payload stream for a host SHA-256 digest.
// Excludes image payloads and physical storage/compression details.
bool WriteNickNonImageState(const std::string &source, const std::string &output, std::string &error);
} // namespace ThreeMM
#endif
