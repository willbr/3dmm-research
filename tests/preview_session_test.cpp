#include "mm/preview.h"
#include <cstdio>
#include <cstdlib>
#include <utility>
#include "preview_admission_fixture.h"
int main(int argc, char **argv)
{
    if (argc != 6)
        return 2;
    MM::Preview first, second;
    MM::Error error;
    AdmissionTrace trace("MM_PREVIEW_SESSION_SNAPSHOT");
    if (!trace.file)
        return 1;
    const auto result = [&](bool actual, bool expected, const MM::Preview &p) {
        if (actual != expected)
            std::fprintf(stderr, "Session result: %s\n", error.message.c_str());
        return actual == expected && trace.Error(actual, error, p);
    };
    MM::PreviewView view;
    view.width = view.height = 64;
    view.stableOrbit = true;
    MM::PreviewImage image, before;
    const auto render = [&] { return result(first.Render(view, image, error), true, first) && trace.Image(image); };
    const auto sources = [&] {
        return result(first.AddContent(1, argv[4], error), true, first) &&
               result(first.AddContent(1, argv[3], error), true, first) &&
               result(first.AddContent(1, argv[5], error), true, first);
    };
    if (!result(first.AddContent(1, argv[3], error), false, first) ||
        !result(first.OpenEmpty("missing-preview-resources.chk", error), false, first) ||
        !result(first.OpenEmpty(argv[1], error), true, first) ||
        !result(first.Select({0x544d504c, 0x2010}, error), false, first) ||
        !result(first.AddContent(0, argv[3], error), false, first) ||
        !result(first.AddContent(-1, argv[3], error), false, first) ||
        !result(first.AddContent(1, "missing-preview-content.3cn", error), false, first) ||
        !result(second.OpenEmpty(argv[1], error), false, second) ||
        !result(first.Open(argv[3], argv[1], error), true, first) ||
        !result(first.Select({0x544d504c, 0x2010}, error), true, first) || !render())
        return 1;
    before = image;
    if (!result(first.Open(argv[3], "missing-preview-resources.chk", error), false, first) || !render() ||
        image.rgba != before.rgba)
        return 1;
    MM::Archive unopened;
    if (!result(first.Open(std::move(unopened), argv[1], error), false, first) || !render() ||
        image.rgba != before.rgba || !result(second.OpenEmpty("missing-preview-resources.chk", error), false, second) ||
        !result(first.Open(argv[2], argv[1], error), true, first) ||
        !result(first.Select({0x5343454e, 0}, error), false, first) ||
        !result(first.AddContent(2, argv[3], error), true, first) || !sources() ||
        !result(first.AddContent(1, argv[3], error), true, first) ||
        !result(first.AddContent(1, "missing-preview-content.3cn", error), false, first) ||
        !result(first.Select({0x5343454e, 0}, error), true, first) || !render())
        return 1;
    MM::Archive movie;
    if (!movie.Open(argv[2], error) || !result(first.Open(std::move(movie), argv[1], error, true), true, first) ||
        !result(first.Select({0x5343454e, 0}, error), false, first) || !sources() ||
        !result(first.Select({0x5343454e, 0}, error), true, first) || !render())
        return 1;
    first.ClearScene();
    if (!result(first.Render(view, image, error), false, first))
        return 1;
    first.Close();
    if (!result(second.OpenEmpty(argv[1], error), true, second) ||
        !result(first.OpenEmpty(argv[1], error), false, first))
        return 1;
    second.Close();
    if (!result(first.OpenEmpty(argv[1], error), true, first) ||
        !result(first.AddContent(1, argv[3], error), true, first))
        return 1;
    if (!trace.Finish("session", 89578, 0x34267E40U))
        return 1;
    std::puts(
        "Preview session: replacement isolation, source ordering/duplicates, runtime ownership and restart passed.");
    return 0;
}
