/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/preview.h"
#include <cmath>
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
    unsigned backgrounds = 0, cameras = 0, depthCameras = 0, meshCameras = 0;
    ThreeMM::PreviewView view;
    view.width = 256;
    view.height = 192;
    view.includeDepth = true;
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
        ThreeMM::PreviewImage firstOrbit;
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
            // No actors are selected, so every finite distance must come from
            // the authored ZBMP. Colour-only backgrounds cannot pass this check.
            if (image.depth.size() != std::size_t(view.width) * view.height)
                return 1;
            bool hasDepth = false;
            for (const auto distance : image.depth)
                if (std::isfinite(distance) && distance > 0)
                {
                    ++depthCameras;
                    hasDepth = true;
                    break;
                }
            const auto authored = image.rgba;
            auto orbit = view;
            orbit.authoredCamera = false;
            orbit.frameSceneCamera = true;
            orbit.yaw = 20;
            orbit.pitch = 15;
            if (!check(preview.Render(orbit, image, error)) || bool(image.coveredPixels) != hasDepth)
                return 1;
            if (image.coveredPixels)
                ++meshCameras;
            if (!camera)
                firstOrbit = image;
            auto fly = orbit;
            fly.freeCamera = true;
            fly.cameraX = image.camera.x;
            fly.cameraY = image.camera.y;
            fly.cameraZ = image.camera.z;
            if (!check(preview.Render(fly, image, error)) || bool(image.coveredPixels) != hasDepth ||
                !check(preview.Render(view, image, error)) || image.rgba != authored)
                return 1;
            ++cameras;
        }
        // Revisit the first camera after cache eviction. Reconstruction must
        // be deterministic, and inspection must not alter authored images.
        auto orbit = view;
        orbit.authoredCamera = false;
        orbit.frameSceneCamera = true;
        orbit.yaw = 20;
        orbit.pitch = 15;
        if (!check(preview.SetCamera(0, error)) || !check(preview.Render(orbit, image, error)) ||
            image.rgba != firstOrbit.rgba || image.depth != firstOrbit.depth)
            return 1;
        if (preview.SetCamera(count, error) || error.code != ThreeMM::ErrorCode::invalid_argument)
            return 1;
    }
    if (!backgrounds || !cameras || !depthCameras || !meshCameras)
    {
        std::fprintf(stderr, "Background cameras must supply authored depth.\n");
        return 1;
    }
    std::printf("Background preview: %u backgrounds, %u cameras with palette, %u with finite depth, %u with meshes.\n",
                backgrounds, cameras, depthCameras, meshCameras);
    return 0;
}
