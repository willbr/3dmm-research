/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_PREVIEW_BACKGROUND_H
#define THREE_MM_PREVIEW_BACKGROUND_H
#include <vector>
#include "soc_core.h"
namespace ThreeMM
{
namespace Internal
{
class PreviewBackground
{
    PChunkyResourceFile source = nullptr; // Borrowed from the preview's source set.
    ChunkNumber number = 0;

  public:
    CameraPosition camera = {};
    std::vector<ChunkNumber> cameras;
    std::vector<LightPosition> lights;
    std::vector<byte> colours;
    std::vector<std::uint16_t> depths;
    std::uint32_t width = 0, height = 0;
    Color palette[256] = {};
    bool Load(PChunkyResourceFile file, ChunkNumber background, const Color *basePalette);
    bool SetCamera(std::uint32_t slot);
};
} // namespace Internal
} // namespace ThreeMM
#endif
