/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/preview.h"
#include <cstdio>
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
    if (!check(preview.Select({0x5343454e, 1}, error)) || !check(preview.Render(view, repeated, error)) ||
        repeated.rgba == first.rgba || !check(preview.Select({0x5343454e, 0}, error)))
        return 1;
    view.authoredCamera = false;
    if (!check(preview.Render(view, first, error)) || !first.coveredPixels || !check(preview.SetFrame(5, error)) ||
        preview.Info().currentCamera != 1 || !check(preview.Render(view, later, error)) || !later.coveredPixels ||
        later.rgba == first.rgba || !check(preview.SetFrame(1, error)) ||
        !check(preview.Render(view, repeated, error)) || first.rgba != repeated.rgba ||
        preview.Info().currentCamera != 0)
        return 1;
    if (preview.SetFrame(11, error) || error.code != ThreeMM::ErrorCode::invalid_argument)
        return 1;
    if (!check(preview.Select({0x41435452, 1}, error)) || preview.Info().firstFrame != 1 ||
        preview.Info().lastFrame <= 10 || !check(preview.Render(view, repeated, error)) || !repeated.coveredPixels)
        return 1;
    std::puts("Scene preview: actors, prop, 3D text/font, old actor header, inactive actor, source resolution, frames "
              "and rewind passed.");
    return 0;
}
