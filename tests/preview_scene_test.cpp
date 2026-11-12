/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/preview.h"
#include <cstdio>
#include <cstring>
#include <cmath>
int main(int argc, char **argv)
{
    if (argc != 6)
        return 2;
    ThreeMM::Preview preview;
    ThreeMM::Error error;
    auto check = [&](bool ok) {
        if (!ok)
            std::fprintf(stderr, "%s\n", error.message.c_str());
        return ok;
    };
    if (!check(preview.Open(argv[2], argv[1], error)))
        return 1;
    if (preview.Select({0x5343454e, 0}, error) || error.message.find("Missing source 1") == std::string::npos)
        return 1;
    if (!check(preview.AddContent(1, argv[3], error)) || !check(preview.AddContent(1, argv[4], error)) ||
        !check(preview.AddContent(1, argv[5], error)) || !check(preview.Select({0x5343454e, 0}, error)))
        return 1;
    const auto info = preview.Info();
    if (!info.scene || !info.timeline || info.actors != 5 || info.firstFrame != 1 || info.lastFrame != 10 ||
        !info.cameraCount)
        return 1;
    ThreeMM::PreviewImage first, later, repeated;
    ThreeMM::PreviewView view;
    if (!check(preview.Render(view, first, error)) || !first.coveredPixels)
        return 1;
    // Metadata evaluation must use a private track and leave the live pose intact.
    const auto objects = preview.Objects();
    const auto soundCount = preview.FrameSounds().size();
    unsigned entrances = 0, actions = 0, spans = 0;
    for (std::size_t object = 0; object < objects.size(); ++object)
    {
        ThreeMM::PreviewAnimationTimeline timeline, again;
        if (!check(preview.ReadAnimationTimeline(static_cast<int>(object), timeline, error)) ||
            !check(preview.ReadAnimationTimeline(static_cast<int>(object), again, error)) ||
            timeline.spans.size() != again.spans.size() || timeline.events.size() != again.events.size())
            return 1;
        for (std::size_t i = 0; i < timeline.spans.size(); ++i)
        {
            const auto &span = timeline.spans[i], &repeat = again.spans[i];
            if (span.firstFrame < info.firstFrame || span.endFrame > info.lastFrame + 1 ||
                span.endFrame <= span.firstFrame || span.firstFrame != repeat.firstFrame ||
                span.endFrame != repeat.endFrame || span.action != repeat.action || span.frozen != repeat.frozen ||
                span.name != repeat.name)
                return 1;
            ++spans;
        }
        for (std::size_t i = 0; i < timeline.events.size(); ++i)
        {
            const auto &event = timeline.events[i], &repeat = again.events[i];
            if (event.frame < info.firstFrame || event.frame > info.lastFrame || event.frame != repeat.frame ||
                event.kind != repeat.kind || event.name != repeat.name || event.detail != repeat.detail)
                return 1;
            entrances += event.kind == ThreeMM::PreviewAnimationKind::Entrance;
            actions += event.kind == ThreeMM::PreviewAnimationKind::Action;
        }
    }
    const auto observed = preview.Objects();
    if (!entrances || !actions || !spans || objects.size() != observed.size() ||
        soundCount != preview.FrameSounds().size())
        return 1;
    for (std::size_t i = 0; i < objects.size(); ++i)
        if (objects[i].action != observed[i].action || objects[i].cel != observed[i].cel ||
            objects[i].visible != observed[i].visible || objects[i].x != observed[i].x ||
            objects[i].y != observed[i].y || objects[i].z != observed[i].z)
            return 1;
    if (!check(preview.Render(view, repeated, error)) || first.rgba != repeated.rgba)
        return 1;
    if (!check(preview.Select({0x5343454e, 1}, error)) || !check(preview.Render(view, repeated, error)) ||
        repeated.rgba == first.rgba || !check(preview.Select({0x5343454e, 0}, error)))
        return 1;
    view.authoredCamera = false;
    view.includeDepth = true;
    if (!check(preview.Render(view, first, error)) || !first.coveredPixels || !check(preview.SetFrame(5, error)) ||
        preview.Info().currentCamera != 1 || !check(preview.Render(view, later, error)) || !later.coveredPixels ||
        later.rgba == first.rgba || !check(preview.SetFrame(1, error)) ||
        !check(preview.Render(view, repeated, error)) || first.rgba != repeated.rgba ||
        preview.Info().currentCamera != 0)
        return 1;
    for (const auto image : {&first, &later, &repeated})
    {
        if (image->depth.size() * 4 != image->rgba.size())
            return 1;
        for (std::size_t pixel = 0; pixel < image->depth.size(); ++pixel)
            if (image->rgba[pixel * 4 + 3])
            {
                if (!std::isfinite(image->depth[pixel]) || image->depth[pixel] < image->projection.nearClip ||
                    image->depth[pixel] > image->projection.farClip + .01f)
                {
                    std::fprintf(stderr, "Invalid camera-forward depth: %f\n", image->depth[pixel]);
                    return 1;
                }
            }
            else if (!std::isinf(image->depth[pixel]))
                return 1;
    }
    if (first.depth != repeated.depth)
        return 1;
    if (preview.SetFrame(11, error) || error.code != ThreeMM::ErrorCode::invalid_argument)
        return 1;
    view.stableOrbit = true;
    if (!check(preview.Select({0x5343454e, 0}, error)) || !check(preview.Render(view, first, error)) ||
        !check(preview.SetFrame(5, error)) || !check(preview.Render(view, later, error)) ||
        std::memcmp(first.projection.transform, later.projection.transform, sizeof(first.projection.transform)))
        return 1;
    preview.ResetOrbitFraming();
    if (!check(preview.Render(view, repeated, error)) ||
        !std::memcmp(first.projection.transform, repeated.projection.transform, sizeof(first.projection.transform)))
        return 1;
    if (!check(preview.SetFrame(1, error)) || !check(preview.Render(view, later, error)) ||
        std::memcmp(repeated.projection.transform, later.projection.transform, sizeof(later.projection.transform)))
        return 1;
    if (!check(preview.Select({0x41435452, 1}, error)) || preview.Info().firstFrame != 1 ||
        preview.Info().lastFrame <= 10 || !check(preview.Render(view, repeated, error)) || !repeated.coveredPixels)
        return 1;
    std::puts("Scene preview: actors, prop, 3D text/font, old actor header, inactive actor, source resolution, frames "
              "and rewind passed.");
    return 0;
}
