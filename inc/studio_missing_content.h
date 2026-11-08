/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_MISSING_CONTENT_H
#define STUDIO_MISSING_CONTENT_H
#include <cstdint>
#include <cstdio>
#include <string>
namespace StudioContent
{
struct MissingContent
{
    std::string source, folder, root;
    std::int32_t sourceId = 0;
    bool hasAsset = false;
    std::uint32_t assetType = 0, assetNumber = 0;
};
inline bool FrankieAsset(const MissingContent &content)
{
    if (!content.hasAsset)
        return false;
    const auto number = content.assetNumber;
    // Identities verified against SCENEPACK1, 3DFONTPACK1, MTRLPACK1,
    // MTRLPACK1-2, legacy MTRLPACK1old and SOLIDMTRLPACK. Other add-ons
    // can share the Expansions source, so its name alone is insufficient.
    if (content.assetType == 0x424b4744) // BKGD
        return number >= 262144 && number <= 262147;
    if (content.assetType == 0x54444620) // TDF
        return number >= 65536 && number <= 65538;
    if (content.assetType == 0x4d54524c) // MTRL
        return number <= 27 || (number >= 327676 && number <= 327737) || (number >= 500000 && number <= 500135);
    if (content.assetType == 0x544d504c) // TMPL
        return number == 270362 || number == 270364 || number == 270366 || number == 270375 || number == 270377;
    return false;
}
inline std::string PackName(const MissingContent &content)
{
    // Source IDs vary between installations. Identify standard packs by their
    // recorded folder aliases; preserve custom sources without guessing.
    auto name = content.folder;
    for (auto &ch : name)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    if (name == "expansions")
        return FrankieAsset(content) ? "Frankie's 3dmm Expansion Pack 1" : "Expansions (legacy add-ons)";
    if (name == "doraemon")
        return "Doraemon Expansion";
    if (name == "n3dmmexp" || name == "n3dmm expansion")
        return "Nickelodeon Expansion";
    if (name == "3dmovie" || name == "3d movie maker")
        return "3D Movie Maker";
    return content.source;
}
inline std::string MissingContentText(const MissingContent &content)
{
    auto path = content.root;
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path += '/';
    path += content.folder;
    std::string text = "This movie needs:\n" + PackName(content) +
                       "\n\nThe content was not found in this installation.\n\nExpected folder:\n" + path +
                       "\n\nInstall or copy the content there, then choose Retry.\n\nSource: " + content.source +
                       " (ID " + std::to_string(content.sourceId) + ')';
    if (content.hasAsset)
    {
        char identity[40];
        const auto type = content.assetType;
        std::snprintf(identity, sizeof(identity), "\nAsset: %c%c%c%c:%08X", static_cast<char>(type >> 24),
                      static_cast<char>(type >> 16), static_cast<char>(type >> 8), static_cast<char>(type),
                      static_cast<unsigned>(content.assetNumber));
        text += identity;
    }
    return text;
}
} // namespace StudioContent
#endif
