/* Headless Preview render/controller compatibility probe. */
#ifndef PREVIEW_RENDER_FIXTURE_H
#define PREVIEW_RENDER_FIXTURE_H
#include "mm/preview_rasterizer.h"
#include <new>
#include <vector>
class RenderProbeRasterizer : public MM::PreviewRasterizer
{
  public:
    bool failBegin = false, failEnd = false;
    int throwAt = 0;
    unsigned triangles = 0, frames = 0;
    std::vector<unsigned char> rgba;
    void Frame(const MM::PreviewProjection &, const unsigned char *, bool) override
    {
        if (throwAt == 1)
            throw std::bad_alloc();
        ++frames;
    }
    bool Begin(unsigned width, unsigned height, MM::Error &error) override
    {
        if (throwAt == 2)
            throw std::bad_alloc();
        triangles = 0;
        if (failBegin)
        {
            error.code = MM::ErrorCode::out_of_memory;
            error.message = "Synthetic Begin failure";
            return false;
        }
        rgba.assign(std::size_t(width) * height * 4, 0);
        for (std::size_t i = 0; i < rgba.size(); i += 4)
        {
            rgba[i] = 17;
            rgba[i + 1] = 89;
            rgba[i + 2] = 201;
            rgba[i + 3] = 255;
        }
        return true;
    }
    void Triangle(const BrRasterTriangle &) noexcept override
    {
        ++triangles;
    }
    bool End(unsigned char *colour, int colourStride, unsigned char *depth, int depthStride,
             MM::Error &error) override
    {
        if (throwAt == 3)
            throw std::bad_alloc();
        if (failEnd)
        {
            error.code = MM::ErrorCode::out_of_memory;
            error.message = "Synthetic End failure";
            return false;
        }
        (void)colour;
        (void)colourStride;
        (void)depth;
        (void)depthStride;
        return true;
    }
    const unsigned char *RGBA() const override
    {
        if (throwAt == 4)
            throw std::bad_alloc();
        return rgba.data();
    }
};
static bool TestPreviewRender(MM::Preview &preview)
{
    FILE *snapshot = nullptr;
    if (auto path = std::getenv("MM_PREVIEW_RENDER_SNAPSHOT"))
        snapshot = std::fopen(path, "w+b");
    else
        snapshot = std::tmpfile();
    if (!snapshot)
        return false;
    std::uint32_t bytesRecorded = 0, fingerprint = 2166136261U;
    auto record = [&](const void *p, std::size_t bytes) {
        auto data = static_cast<const unsigned char *>(p);
        for (std::size_t i = 0; i < bytes; ++i)
            fingerprint = (fingerprint ^ data[i]) * 16777619U;
        bytesRecorded += static_cast<std::uint32_t>(bytes);
        return std::fwrite(p, 1, bytes, snapshot) == bytes;
    };
    auto image = [&](const MM::PreviewImage &out) {
        std::uint32_t fields[] = {out.width, out.height, out.coveredPixels, static_cast<std::uint32_t>(out.rgba.size()),
                                  static_cast<std::uint32_t>(out.depth.size())};
        const auto &c = out.camera;
        float camera[] = {c.x, c.y, c.z, c.yaw, c.pitch, c.moveSpeed, c.roll, c.verticalFov};
        if (!record(fields, sizeof(fields)) || !record(camera, sizeof(camera)))
            return false;
        for (auto p : {&out.projection, &out.sceneCamera})
        {
            std::int32_t flags[] = {p->valid, p->orthographic};
            float fields[] = {p->verticalFov, p->aspect, p->nearClip, p->farClip, p->displayDepth, p->orthoHeight};
            if (!record(flags, sizeof(flags)) || !record(fields, sizeof(fields)) ||
                !record(p->transform, sizeof(p->transform)))
                return false;
        }
        return record(out.rgba.data(), out.rgba.size()) && record(out.depth.data(), out.depth.size() * sizeof(float));
    };
    MM::Error error;
    bool ok = preview.Select({0x5343454e, 0}, error);
    for (int configuration = 0; ok && configuration < 12; ++configuration)
    {
        preview.ResetOrbitFraming();
        for (int mode = 0; ok && mode < 7; ++mode)
            for (int frame : {1, 5})
            {
                MM::PreviewView view;
                view.width = configuration % 3 == 0 ? 128 : configuration % 3 == 1 ? 96 : 160;
                view.height = configuration % 3 == 1 ? 128 : 96;
                view.includeDepth = true;
                view.displayMode = mode;
                view.authoredCamera = configuration == 0 || configuration == 11;
                view.renderAtOutputResolution = configuration == 11;
                view.fitSceneDepth = configuration >= 2;
                view.yaw = 23.5f;
                view.pitch = -11.25f;
                view.roll = 7.5f;
                view.zoom = 1.15f;
                view.verticalFov = 53;
                view.orthographic =
                    configuration == 3 || configuration == 5 || configuration == 6 || configuration == 7;
                view.freeCamera = configuration == 4 || configuration == 5 || configuration == 10;
                view.cameraX = 2;
                view.cameraY = 8;
                view.cameraZ = 25;
                view.axisView = configuration == 6 ? 2 : configuration == 7 ? 3 : 0;
                view.frameSceneCamera = configuration == 8 || configuration == 10;
                view.stableOrbit = configuration == 9 || configuration == 10;
                view.orbitObject = configuration == 9 ? 0 : -1;
                if (configuration == 2)
                    view.nearClip = .025f;
                MM::PreviewImage output;
                ok = preview.SetFrame(frame, error) && preview.Render(view, output, error);
                std::int32_t fields[] = {configuration, mode, frame, ok, static_cast<std::int32_t>(error.code)};
                ok = ok && record(fields, sizeof(fields)) && image(output);
            }
    }
    MM::PreviewView view;
    view.width = 128;
    view.height = 96;
    view.includeDepth = true;
    view.authoredCamera = false;
    MM::PreviewImage before, after;
    ok = ok && preview.Select({0x5343454e, 0}, error) && preview.Render(view, before, error);
    for (int failure = 0; ok && failure < 3; ++failure)
    {
        RenderProbeRasterizer rasterizer;
        rasterizer.failBegin = failure == 1;
        rasterizer.failEnd = failure == 2;
        view.rasterizer = &rasterizer;
        view.displayMode = 4;
        MM::PreviewImage output;
        bool rendered = preview.Render(view, output, error);
        std::int32_t fields[] = {failure, rendered, static_cast<std::int32_t>(error.code),
                                 static_cast<std::int32_t>(rasterizer.frames),
                                 static_cast<std::int32_t>(rasterizer.triangles)};
        ok = rendered == !failure && record(fields, sizeof(fields)) && image(output);
        view.rasterizer = nullptr;
        view.displayMode = 0;
        ok = ok && preview.Render(view, after, error) && before.rgba == after.rgba && before.depth == after.depth;
    }
    // Exceptions from foreign rasterizers return through the bridge only after
    // the C owner has restored materials, disabled lights and reset the sink.
    for (int stage = 1; ok && stage <= 4; ++stage)
    {
        RenderProbeRasterizer rasterizer;
        rasterizer.throwAt = stage;
        view.rasterizer = &rasterizer;
        view.displayMode = 1;
        MM::PreviewImage output;
        ok = !preview.Render(view, output, error) && error.code == MM::ErrorCode::out_of_memory &&
             output.rgba.empty() && !output.width;
        view.rasterizer = nullptr;
        view.displayMode = 0;
        ok = ok && preview.Render(view, after, error) && before.rgba == after.rgba && before.depth == after.depth;
    }
    ok = std::fclose(snapshot) == 0 && ok;
#ifdef _WIN64
    ok = ok && bytesRecorded == 18029304 && fingerprint == 0xA31B99A6U;
#endif
    std::printf("Preview render trace: bytes=%u fingerprint=%08X\n", bytesRecorded, fingerprint);
    if (!ok)
        std::fprintf(stderr, "Render controller probe: %s\n", error.message.c_str());
    return ok;
}
#endif
