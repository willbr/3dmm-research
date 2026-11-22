/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/preview.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <limits>
#include "preview_admission_fixture.h"
#include "preview_render_fixture.h"
#include "preview_pick_fixture.h"
#include "preview_camera_fixture.h"
#include "preview_live_fixture.h"

static bool CaptureActorCatalogue(MM::Preview &preview)
{
    if (!std::getenv("MM_PREVIEW_ACTOR_CATALOGUE_SNAPSHOT"))
        return true;
    AdmissionTrace trace("MM_PREVIEW_ACTOR_CATALOGUE_SNAPSHOT");
    MM::Error error;
    auto string = [&](const std::string &value) {
        const auto count = static_cast<std::uint32_t>(value.size());
        return trace.Record(&count, sizeof(count)) && trace.Record(value.data(), count);
    };
    const auto objects = preview.Objects();
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        const int object = static_cast<int>(i);
        std::vector<MM::PreviewAction> actions;
        std::vector<MM::PreviewCostume> costumes;
        MM::PreviewObjectText text;
        const bool hasActions = preview.ObjectActions(object, actions, error);
        const bool hasCostumes = preview.ObjectCostumes(object, costumes, error);
        const bool hasText = preview.ObjectText(object, text, error);
        const std::uint32_t fields[] = {hasActions, static_cast<std::uint32_t>(actions.size()),
                                        hasCostumes, static_cast<std::uint32_t>(costumes.size()), hasText};
        if (!trace.Record(fields, sizeof(fields)))
            return false;
        for (const auto &action : actions)
            if (!string(action.name) || !trace.Record(&action.celCount, sizeof(action.celCount)))
                return false;
        for (const auto &costume : costumes)
        {
            const int row[] = {costume.bodySet, costume.costume, costume.selected, costume.accessory};
            if (!trace.Record(row, sizeof(row)))
                return false;
        }
        if (hasText && (!string(text.text) || !trace.Record(&text.shape, sizeof(text.shape)) ||
                        !trace.Record(&text.font, sizeof(text.font)) || !trace.Record(&text.fontSource, sizeof(text.fontSource))))
            return false;
    }
    return trace.Finish("actor catalogue", 0, 0);
}

static bool CaptureActorTransforms(MM::Preview &preview)
{
    if (!std::getenv("MM_PREVIEW_ACTOR_TRANSFORMS_SNAPSHOT"))
        return true;
    AdmissionTrace trace("MM_PREVIEW_ACTOR_TRANSFORMS_SNAPSHOT");
    MM::Error error;
    auto read = [&](int object) {
        MM::PreviewTrackPose pose;
        MM::PreviewBounds bounds;
        const bool tracked = preview.ObjectTrackPose(object, pose);
        if (!preview.ObjectBounds(object, bounds, error))
            return false;
        const std::int32_t route[] = {tracked, pose.node, pose.offset, pose.routeFrame};
        const std::uint32_t box[] = {bounds.valid, bounds.colour};
        return trace.Record(route, sizeof(route)) && trace.Record(pose.position, sizeof(pose.position)) &&
               trace.Record(pose.translation, sizeof(pose.translation)) &&
               trace.Record(pose.rotation, sizeof(pose.rotation)) && trace.Record(box, sizeof(box)) &&
               trace.Record(bounds.corners, sizeof(bounds.corners));
    };
    for (int frame : {1, 5, 10})
    {
        if (!preview.SetFrame(frame, error))
            return false;
        const auto objects = preview.Objects();
        for (std::size_t i = 0; i < objects.size(); ++i)
            if (!read(static_cast<int>(i)))
                return false;
    }
    if (!preview.SetFrame(1, error))
        return false;
    preview.ResetOrbitFraming();
    MM::PreviewView view;
    view.width = 96;
    view.height = 72;
    view.yaw = 24;
    view.pitch = 17;
    view.authoredCamera = false;
    view.stableOrbit = true;
    view.orbitObject = view.onlyObject = 0;
    auto render = [&] {
        MM::PreviewImage image;
        return preview.Render(view, image, error) && image.coveredPixels &&
               trace.Record(image.projection.transform, sizeof(image.projection.transform)) && trace.Image(image);
    };
    if (!render())
        return false;
    MM::PreviewObjectShape shape;
    shape.scale[0] = 1.6f;
    shape.scale[2] = .65f;
    shape.rotation[0] = shape.rotation[8] = 0;
    shape.rotation[2] = -1;
    shape.rotation[6] = 1;
    if (!preview.SetObjectShape(0, shape, error) || !read(0) || !render())
        return false;
    for (int i = 0; i < 5; ++i)
    {
        auto invalid = shape;
        switch (i)
        {
        case 0: invalid.scale[0] = 0; break;
        case 1: invalid.rotation[0] = std::numeric_limits<float>::quiet_NaN(); break;
        case 2: invalid.rotation[2] = 1; break;
        case 3: invalid.rotation[2] = -1.005f; break;
        case 4: invalid.scale[0] = std::numeric_limits<float>::infinity(); break;
        }
        const bool applied = preview.SetObjectShape(0, invalid, error);
        if (applied || error.code != MM::ErrorCode::invalid_argument || !trace.Error(applied, error, preview))
            return false;
    }
    if (!read(0))
        return false;
    MM::PreviewBounds overrideBounds;
    if (!preview.ObjectBounds(0, overrideBounds, error) || !overrideBounds.valid)
        return false;
    for (int i = 0; i < 24; i += 3)
        overrideBounds.corners[i] += 4;
    view.orbitBounds = &overrideBounds;
    view.stableOrbit = false;
    if (!render())
        return false;
    overrideBounds.valid = false;
    if (!render() || !preview.SetObjectShape(0, {}, error))
        return false;
    preview.ResetOrbitFraming();
    return trace.Finish("actor transforms", 0, 0);
}

int main(int argc, char **argv)
{
    if (argc != 6)
        return 2;
    MM::Preview preview;
    MM::Error error;
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
    const auto &info = preview.Info();
    if (!info.scene || !info.timeline || info.actors != 5 || info.firstFrame != 1 || info.lastFrame != 10 ||
        !info.cameraCount)
        return 1;
    MM::PreviewImage first, later, repeated;
    MM::PreviewView view;
    if (!check(preview.Render(view, first, error)) || !first.coveredPixels || !CaptureActorCatalogue(preview))
        return 1;
    // Metadata evaluation must use a private track and leave the live pose intact.
    const auto objects = preview.Objects();
    const auto soundCount = preview.FrameSounds().size();
    unsigned entrances = 0, actions = 0, spans = 0;
    for (std::size_t object = 0; object < objects.size(); ++object)
    {
        MM::PreviewAnimationTimeline timeline, again;
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
            entrances += event.kind == MM::PreviewAnimationKind::Entrance;
            actions += event.kind == MM::PreviewAnimationKind::Action;
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
        info.currentFrame != 5 || info.currentCamera != 1 || !check(preview.Render(view, later, error)) ||
        !later.coveredPixels || later.rgba == first.rgba)
        return 1;
    MM::PreviewLight light;
    light.yaw = 42;
    light.pitch = -20;
    MM::PreviewLight point;
    point.x = -6;
    point.y = 13;
    point.z = 27;
    point.intensity = .4f;
    point.point = true;
    MM::PreviewLight inactive = point;
    inactive.intensity = 0;
    MM::PreviewImage lit, litPoint, mixed, unlit, inactiveOnly;
    if (!check(preview.RenderWithLights(view, {light}, lit, error)) ||
        !check(preview.RenderWithLights(view, {point}, litPoint, error)) ||
        !check(preview.RenderWithLights(view, {light, point, inactive}, mixed, error)) ||
        !check(preview.RenderWithLights(view, {}, unlit, error)) || !lit.coveredPixels ||
        !check(preview.RenderWithLights(view, {inactive}, inactiveOnly, error)) ||
        inactiveOnly.rgba != unlit.rgba || lit.depth != unlit.depth || litPoint.depth != unlit.depth ||
        mixed.depth != unlit.depth || !check(preview.Render(view, repeated, error)) ||
        repeated.rgba != later.rgba || repeated.depth != later.depth)
        return 1;
    if (const auto *path = std::getenv("MM_PREVIEW_LIGHT_SNAPSHOT"))
    {
        FILE *snapshot = std::fopen(path, "wb");
        if (!snapshot)
            return 1;
        bool written = true;
        for (const auto *frame : {&lit, &litPoint, &mixed, &unlit, &repeated})
            written = std::fwrite(frame->rgba.data(), 1, frame->rgba.size(), snapshot) == frame->rgba.size() &&
                      std::fwrite(frame->depth.data(), sizeof(float), frame->depth.size(), snapshot) == frame->depth.size() &&
                      written;
        if (std::fclose(snapshot) || !written)
            return 1;
    }
    const auto cameras = info.cameraCount;
    if (preview.SetBackground(999, 0, error) || error.code != MM::ErrorCode::decode_failed ||
        info.currentFrame != 5 || info.currentCamera != 1 || info.cameraCount != cameras ||
        !check(preview.Render(view, repeated, error)) || repeated.rgba != later.rgba)
        return 1;
    if (!check(preview.SetBackground(1, 0, error)) || info.currentFrame != 5 || info.currentCamera != 1 ||
        info.cameraCount != cameras || !check(preview.Render(view, repeated, error)) || repeated.rgba != later.rgba)
        return 1;
    if (!check(preview.SetFrame(1, error)) ||
        !check(preview.Render(view, repeated, error)) || first.rgba != repeated.rgba ||
        info.currentFrame != 1 || info.currentCamera != 0)
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
    if (preview.SetFrame(11, error) || error.code != MM::ErrorCode::invalid_argument)
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
    if (!CaptureActorTransforms(preview))
        return 1;
    if (!check(preview.Select({0x41435452, 1}, error)) || preview.Info().firstFrame != 1 ||
        preview.Info().lastFrame <= 10 || !check(preview.Render(view, repeated, error)) || !repeated.coveredPixels)
        return 1;
    if(!TestPreviewLive(preview) || !TestPreviewCamera(preview) || !TestPreviewPick(preview) || !TestPreviewRender(preview)) return 1;
    std::puts("Scene preview: actors, prop, 3D text/font, old actor header, inactive actor, source resolution, frames "
              "and rewind passed.");
    return 0;
}
