/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/preview.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    ThreeMM::Preview preview;
    ThreeMM::Archive archive;
    ThreeMM::Error error;
    auto check = [&](bool ok) {
        if (!ok)
            std::fprintf(stderr, "%s\n", error.message.c_str());
        return ok;
    };
    if (!check(archive.Open(argv[2], error)) || !check(preview.Open(argv[2], argv[1], error)))
        return 1;
    unsigned backgrounds = 0, cameras = 0;
    ThreeMM::PreviewView view;
    view.width = 256;
    view.height = 192;
    ThreeMM::PreviewImage image;
    for (const auto &chunk : archive.Chunks())
    {
        if (ThreeMM::TypeLabel(chunk.id.type) != "BKGD")
            continue;
        if (std::getenv("THREE_MM_PREVIEW_TRACE"))
            std::fprintf(stderr, "Previewing %s\n", ThreeMM::Identity(chunk.id).c_str());
        if (!check(preview.Select(chunk.id, error)))
            return 1;
        ++backgrounds;
        const auto count = preview.Info().cameraCount;
        for (unsigned camera = 0; camera < count; ++camera)
        {
            if (!check(preview.SetCamera(camera, error)) || !check(preview.Render(view, image, error)) ||
                !image.coveredPixels)
                return 1;
            // An opaque single-colour placeholder cannot pass as a background.
            unsigned different = 0;
            for (std::size_t i = 4; i < image.rgba.size(); i += 4)
                if (image.rgba[i] != image.rgba[0] || image.rgba[i + 1] != image.rgba[1] ||
                    image.rgba[i + 2] != image.rgba[2])
                    ++different;
            if (different < 1024)
                return 1;
            ++cameras;
        }
        if (preview.SetCamera(count, error) || error.code != ThreeMM::ErrorCode::invalid_argument)
            return 1;
    }
    if (!backgrounds || !cameras)
        return 1;
    std::printf("Background preview: %u backgrounds, %u cameras with palette and depth.\n", backgrounds, cameras);
    return 0;
}
