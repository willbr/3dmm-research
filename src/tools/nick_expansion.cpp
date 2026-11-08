/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/nick_expansion.h"
#include <cstdio>
#include <cstring>
#include "kauai_core.h"
#undef size
ASSERTNAME
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%d %s\n", file, int(line), message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    return false;
}
int main(int argc, char **argv)
{
    std::string error;
    if (argc == 4 && !std::strcmp(argv[1], "non-image-state"))
    {
        if (ThreeMM::WriteNickNonImageState(argv[2], argv[3], error))
            return 0;
        std::fprintf(stderr, "nick-expansion: %s\n", error.c_str());
        return 1;
    }
    if (argc == 4 && !std::strcmp(argv[1], "export-images"))
    {
        if (ThreeMM::ExportNickImages(argv[2], argv[3], error))
            return 0;
        std::fprintf(stderr, "nick-expansion: %s\n", error.c_str());
        return 1;
    }
    if (argc == 5 && !std::strcmp(argv[1], "replace-images"))
    {
        if (ThreeMM::ReplaceNickImages(argv[2], argv[3], argv[4], error))
            return 0;
        std::fprintf(stderr, "nick-expansion: %s\n", error.c_str());
        return 1;
    }
    if (argc == 6 && !std::strcmp(argv[1], "convert") && !std::strcmp(argv[2], "--nearest-palette"))
    {
        ThreeMM::NickConversionStats stats;
        // output must already be empty; the ISO wrapper owns staging/publishing.
        // Keep the policy explicit so a historical pixel match is never implied.
        if (!ThreeMM::ConvertNickExpansion(argv[3], argv[4], argv[5], stats, error))
        {
            std::fprintf(stderr, "nick-expansion: %s\n", error.c_str());
            return 1;
        }
        std::printf("Prepared intermediate candidate: %zu archives, %zu chunks, %zu images and %zu palettes.\n"
                    "Historical image fixups still need to be applied and verified.\n",
                    stats.archives, stats.chunks, stats.images, stats.palettes);
        return 0;
    }
    if (argc == 4 && !std::strcmp(argv[1], "compare"))
    {
        ThreeMM::NickComparison result;
        if (!ThreeMM::CompareNickExpansion(argv[2], argv[3], result, error))
        {
            std::fprintf(stderr, "nick-expansion: %s\n", error.c_str());
            return 1;
        }
        std::printf("Archives: %zu\nChunks: %zu\nImage/palette differences: %zu\nOther differences: %zu\n",
                    result.archives, result.chunks, result.imageDifferences, result.otherDifferences);
        if (!result.firstDifference.empty())
            std::printf("First difference: %s\n", result.firstDifference.c_str());
        return result.otherDifferences ? 1 : result.imageDifferences ? 3 : 0;
    }
    std::fputs(
        "usage: nick-expansion convert --nearest-palette DISC_DIRECTORY BASE_3DMOVIE.CHK EMPTY_OUTPUT_DIRECTORY\n"
        "       nick-expansion compare OUTPUT_DIRECTORY REFERENCE_DIRECTORY\n"
        "       nick-expansion export-images EXPANSION_DIRECTORY EMPTY_OUTPUT_DIRECTORY\n"
        "       nick-expansion non-image-state EXPANSION_DIRECTORY OUTPUT_FILE\n"
        "       nick-expansion replace-images EXPANSION_DIRECTORY IMAGE_FIXUP_DIRECTORY EMPTY_OUTPUT_DIRECTORY\n",
        stderr);
    return 2;
}
