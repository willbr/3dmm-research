/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "mm/preview.h"
#include "preview_admission_fixture.h"
#ifdef size
#error Legacy macros leaked through the preview API
#endif
#if defined(NewObj) || defined(WIN) || defined(STRICT)
#error Preview API leaked legacy definitions
#endif
#ifdef __has_include
#if __has_include("soc_core.h") || __has_include("kauai_core.h")
#error Preview target exposed private include directories
#endif
#endif

static bool Check(bool ok, const char *step, const MM::Error &error)
{
    if (!ok)
        std::fprintf(stderr, "%s: %s\n", step, error.message.c_str());
    return ok;
}
int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    MM::Preview preview;
    MM::Archive archive;
    MM::Error error;
    AdmissionTrace trace;
    if (!trace.file) return 1;
    if (!Check(archive.Open(argv[2], error), "open archive", error) ||
        !Check(preview.Open(argv[2], argv[1], error), "open preview", error))
        return 1;
    unsigned actors = 0, props = 0;
    MM::PreviewView view;
    view.width = 256;
    view.height = 256;
    MM::PreviewImage image, repeated;
    for (const auto &chunk : archive.Chunks())
    {
        if (MM::TypeLabel(chunk.id.type) != "TMPL")
            continue;
        if (std::getenv("MM_PREVIEW_TRACE"))
            std::fprintf(stderr, "Previewing %s: %s\n", MM::Identity(chunk.id).c_str(), chunk.name.c_str());
        if (!Check(preview.Select(chunk.id, error), MM::Identity(chunk.id).c_str(), error) ||
            !Check(preview.Render(view, image, error), "render", error))
            return 1;
        const auto &info = preview.Info();
        if (!trace.Info(preview) || !trace.Image(image)) return 1;
        if (info.prop)
            ++props;
        else
            ++actors;
        if (!info.parts || !info.triangles || !image.coveredPixels || image.rgba.size() != 256 * 256 * 4)
        {
            std::fprintf(stderr, "%s: empty preview (%u pixels)\n", MM::Identity(chunk.id).c_str(),
                         image.coveredPixels);
            return 1;
        }
        if (!Check(preview.Render(view, repeated, error), "repeat", error) || repeated.rgba != image.rgba)
            return 1;
        if (chunk.id.number == 0x2010)
        {
            FILE *file = std::fopen(argv[3], "wb");
            if (!file)
                return 1;
            std::fprintf(file, "P6\n256 256\n255\n");
            for (std::size_t i = 0; i < image.rgba.size(); i += 4)
                std::fwrite(image.rgba.data() + i, 1, 3, file);
            if (std::fclose(file))
                return 1;
            auto orbit = view;
            orbit.yaw = 65;
            orbit.pitch = 20;
            if (!Check(preview.Render(orbit, repeated, error), "orbit", error) || repeated.rgba == image.rgba)
                return 1;
            const auto last = static_cast<std::uint32_t>(info.actions.size() - 1);
            if (!Check(preview.SetPose(last, info.actions[last].celCount - 1, error), "pose", error) ||
                !Check(preview.Render(view, repeated, error), "render pose", error) || !repeated.coveredPixels)
                return 1;
            if (!trace.Info(preview) || !trace.Image(repeated)) return 1;
            bool posed = preview.SetPose(unsigned(info.actions.size()), 0, error);
            if (posed || !trace.Error(posed, error, preview)) return 1;
            posed = preview.SetPose(last, info.actions[last].celCount, error);
            if (posed || !trace.Error(posed, error, preview)) return 1;
        }
    }
    if (!actors || !props)
        return 1;
    bool rawModelTested = false;
    for (const auto &chunk : archive.Chunks())
        if (MM::TypeLabel(chunk.id.type) == "BMDL")
        {
            if (!Check(preview.Select(chunk.id, error), "raw model", error) ||
                !Check(preview.Render(view, image, error), "raw model render", error))
                return 1;
            if (!preview.Info().triangles)
            {
                if (image.coveredPixels)
                    return 1;
                continue; // Shipped content includes intentional empty models.
            }
            if (!trace.Info(preview) || !trace.Image(image)) return 1;
            if (!image.coveredPixels)
            {
                std::fprintf(stderr, "Raw model %s: %u triangles, zero covered pixels\n",
                             MM::Identity(chunk.id).c_str(), preview.Info().triangles);
                return 1;
            }
            rawModelTested = true;
            break;
        }
    if (!rawModelTested)
        return 1;
    // Failed replacement and a second renderer must leave the live session usable.
    if (preview.Open(argv[2], "missing-preview-resources.chk", error))
        return 1;
    if (!Check(preview.Render(view, image, error), "failed-open isolation", error))
        return 1;
    {
        MM::Preview second;
        if (second.Open(argv[2], argv[1], error) || error.code != MM::ErrorCode::invalid_argument)
            return 1;
    }
    view.zoom = std::numeric_limits<float>::quiet_NaN();
    if (preview.Render(view, image, error) || !image.rgba.empty() || error.code != MM::ErrorCode::invalid_argument)
        return 1;
    preview.Close();
    // Runtime caches must support shutdown/restart in the same process.
    if (!Check(preview.Open(argv[2], argv[1], error), "reopen", error) ||
        !Check(preview.Select({0x544d504c, 0x2010}, error), "reselect", error))
        return 1;
    view.zoom = 1;
    if (!Check(preview.Render(view, image, error), "render after restart", error) || !image.coveredPixels)
        return 1;
    for (MM::Id id : {MM::Id{0x544d504c,0xfffffff0U}, MM::Id{0x424d444c,0xfffffff0U}, MM::Id{0x5343454e,0xfffffff0U}, MM::Id{0x41435452,0xfffffff0U}, MM::Id{0x12345678,0}})
    {
        bool selected = preview.Select(id, error);
        if (selected || !trace.Error(selected, error, preview)) return 1;
    }
    if (!trace.Finish()) return 1;
    std::printf("Preview API: %u actors, %u props; poses, orbit, repeatability and runtime restart passed.\n", actors,
                props);
    return 0;
}
