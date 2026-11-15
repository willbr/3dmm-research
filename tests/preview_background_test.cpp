/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/preview.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
static FILE *snapshot;
static unsigned bytesRecorded, fingerprint=2166136261U;
static void Record(const void *data,std::size_t bytes)
{
    auto p=static_cast<const unsigned char *>(data);
    for(std::size_t i=0;i<bytes;++i)fingerprint=(fingerprint^p[i])*16777619U;
    bytesRecorded+=unsigned(bytes);
    if(snapshot && std::fwrite(data,1,bytes,snapshot)!=bytes)std::exit(1);
}
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    if(auto path=std::getenv("MM_BACKGROUND_PREVIEW_SNAPSHOT")){snapshot=std::fopen(path,"wb");if(!snapshot)return 1;}
    MM::Preview preview;
    MM::Archive archive;
    MM::Error error;
    auto check = [&](bool ok) {
        if (!ok)
            std::fprintf(stderr, "%s\n", error.message.c_str());
        return ok;
    };
    if (!check(archive.Open(argv[2], error)) || !check(preview.Open(argv[2], argv[1], error)))
        return 1;
    unsigned backgrounds = 0, cameras = 0, depthCameras = 0, meshCameras = 0, backdropCameras = 0;
    MM::PreviewView view;
    view.width = 256;
    view.height = 192;
    view.includeDepth = true;
    MM::PreviewImage image;
    auto render=[&](const MM::PreviewView &v,MM::PreviewImage &out) {
        bool ok=preview.Render(v,out,error);
        if(ok){Record(&out.width,sizeof(out.width));Record(&out.height,sizeof(out.height));Record(out.rgba.data(),out.rgba.size());Record(out.depth.data(),out.depth.size()*sizeof(out.depth[0]));}
        return ok;
    };
    for (const auto &chunk : archive.Chunks())
    {
        if (MM::TypeLabel(chunk.id.type) != "BKGD")
            continue;
        if (std::getenv("MM_PREVIEW_TRACE"))
            std::fprintf(stderr, "Previewing %s\n", MM::Identity(chunk.id).c_str());
        if (!check(preview.Select(chunk.id, error)))
            return 1;
        ++backgrounds;
        const auto count = preview.Info().cameraCount;
        MM::PreviewImage firstOrbit;
        for (unsigned camera = 0; camera < count; ++camera)
        {
            if (!check(preview.SetCamera(camera, error)) || !check(render(view, image)) ||
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
            bool hasInfinite = false;
            for (std::size_t i = 0; i < image.depth.size(); ++i)
                if (image.rgba[i * 4 + 3] && !std::isfinite(image.depth[i]))
                    hasInfinite = true;
            const auto finiteDepth = [](const MM::PreviewImage &rendered) {
                return std::any_of(rendered.depth.begin(), rendered.depth.end(),
                                   [](float distance) { return std::isfinite(distance); });
            };
            auto orbit = view;
            orbit.authoredCamera = false;
            orbit.frameSceneCamera = true;
            orbit.yaw = 20;
            orbit.pitch = 15;
            if (!check(render(orbit, image)) || finiteDepth(image) != hasDepth ||
                (hasInfinite && image.coveredPixels != view.width * view.height))
                return 1;
            if (finiteDepth(image))
                ++meshCameras;
            if (hasInfinite)
                ++backdropCameras;
            if (!camera)
                firstOrbit = image;
            auto fly = orbit;
            fly.freeCamera = true;
            fly.cameraX = image.camera.x;
            fly.cameraY = image.camera.y;
            fly.cameraZ = image.camera.z;
            if (!check(render(fly, image)) || finiteDepth(image) != hasDepth ||
                (hasInfinite && image.coveredPixels != view.width * view.height))
                return 1;
            if (!hasDepth && hasInfinite)
            {
                const auto backdrop = image.rgba;
                fly.yaw += 130;
                fly.pitch = -30;
                fly.cameraX += 100;
                fly.cameraY -= 50;
                if (!check(render(fly, image)) || image.rgba != backdrop || finiteDepth(image))
                    return 1;
            }
            if (!check(render(view, image)) || image.rgba != authored)
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
        if (!check(preview.SetCamera(0, error)) || !check(render(orbit, image)) ||
            image.rgba != firstOrbit.rgba || image.depth != firstOrbit.depth)
            return 1;
        if (preview.SetCamera(count, error) || error.code != MM::ErrorCode::invalid_argument)
            return 1;
    }
    if (!backgrounds || !cameras || !depthCameras || !meshCameras || !backdropCameras)
    {
        std::fprintf(stderr, "Background cameras must supply authored depth.\n");
        return 1;
    }
    std::printf("Background preview: %u backgrounds, %u cameras with palette, %u with finite depth, %u with meshes, "
                "%u with static backdrops.\n",
                backgrounds, cameras, depthCameras, meshCameras, backdropCameras);
    if(snapshot && std::fclose(snapshot))return 1;
    std::printf("Background preview trace: bytes=%u fingerprint=%08X\n",bytesRecorded,fingerprint);
    return 0;
}
