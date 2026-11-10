#include "gpu_renderer.h"
#include "player.h"
#include "settings.h"
#include "three_mm/preview.h"
#include "zb_capture.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s (%s)\n", __LINE__, #value, error.message.c_str());                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(int argc, char **argv)
{
    ThreeMM::Error error;
    // Pure settings/API checks also run on machines with no GPU.
    CHECK(marcus_renderer_from_id("brender") == MARCUS_RENDERER_BRENDER);
    CHECK(marcus_renderer_from_id("brender-compute") == MARCUS_RENDERER_BRENDER_COMPUTE);
    CHECK(marcus_renderer_from_id("sdl-gpu") == MARCUS_RENDERER_SDL_GPU);
    CHECK(marcus_renderer_from_id("invalid") == -1 && marcus_renderer_from_id(nullptr) == -1);
    auto player = marcus_create();
    CHECK(player && marcus_get_renderer(player) == MARCUS_RENDERER_BRENDER);
    CHECK(!marcus_set_renderer(player, -1) && !marcus_set_renderer(player, MARCUS_RENDERER_COUNT));
    CHECK(marcus_get_renderer(player) == MARCUS_RENDERER_BRENDER);
    marcus_destroy(player);

    MarcusShortcuts shortcuts = {}, loaded = {};
    shortcuts_defaults(&shortcuts);
    CHECK(shortcuts.renderer == MARCUS_RENDERER_BRENDER);
    CHECK(shortcuts.render_resolution == MARCUS_DEFAULT_RESOLUTION);
    CHECK(shortcuts.work_renderer == MARCUS_RENDERER_BRENDER);
    CHECK(shortcuts.work_render_resolution == MARCUS_DEFAULT_RESOLUTION);
    CHECK(marcus_render_resolution(MARCUS_DEFAULT_RESOLUTION)->width == 544 &&
          marcus_render_resolution(MARCUS_DEFAULT_RESOLUTION)->height == 306);
    CHECK(!marcus_render_resolution(-1) && !marcus_render_resolution(MARCUS_RESOLUTION_COUNT));
    const auto path =
        std::filesystem::temp_directory_path() / ("marcus-renderer-" + std::to_string(SDL_GetTicksNS()) + ".cfg");
    struct RemoveSettings
    {
        std::filesystem::path path;
        ~RemoveSettings()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } removeSettings{path};
    char message[256] = {};
    shortcuts.renderer = MARCUS_RENDERER_SDL_GPU;
    shortcuts.work_renderer = MARCUS_RENDERER_BRENDER_COMPUTE;
    CHECK(shortcuts_save(path.u8string().c_str(), &shortcuts, message, sizeof(message)));
    shortcuts_defaults(&loaded);
    CHECK(shortcuts_load(path.u8string().c_str(), &loaded, message, sizeof(message)));
    CHECK(loaded.renderer == MARCUS_RENDERER_SDL_GPU);
    CHECK(loaded.work_renderer == MARCUS_RENDERER_BRENDER_COMPUTE);
    constexpr int nativeSizes[][2] = {{272, 153}, {544, 306}, {1088, 612}, {1632, 918}, {2176, 1224}};
    for (int index = 0; index < MARCUS_RESOLUTION_COUNT; ++index)
    {
        const auto *resolution = marcus_render_resolution(index);
        CHECK(resolution && resolution->width <= ThreeMM::PreviewMaxDimension &&
              resolution->height <= ThreeMM::PreviewMaxDimension && resolution->width * 9 == resolution->height * 16);
        if (index < 5)
            CHECK(resolution->width == nativeSizes[index][0] && resolution->height == nativeSizes[index][1]);
        shortcuts.render_resolution = index;
        shortcuts.work_render_resolution = MARCUS_RESOLUTION_COUNT - 1 - index;
        CHECK(shortcuts_save(path.u8string().c_str(), &shortcuts, message, sizeof(message)));
        shortcuts_defaults(&loaded);
        CHECK(shortcuts_load(path.u8string().c_str(), &loaded, message, sizeof(message)));
        CHECK(loaded.render_resolution == index && loaded.renderer == MARCUS_RENDERER_SDL_GPU);
        CHECK(loaded.work_render_resolution == MARCUS_RESOLUTION_COUNT - 1 - index &&
              loaded.work_renderer == MARCUS_RENDERER_BRENDER_COMPUTE);
    }
    shortcuts.render_resolution = -1;
    CHECK(!shortcuts_save(path.u8string().c_str(), &shortcuts, message, sizeof(message)));
    shortcuts.render_resolution = MARCUS_DEFAULT_RESOLUTION;
    shortcuts.work_render_resolution = -1;
    CHECK(!shortcuts_save(path.u8string().c_str(), &shortcuts, message, sizeof(message)));
    for (const char *legacy : {
         "marcus-shortcuts 1\nrenderer brender-compute\nrender_resolution 1920x1440\n",
         "marcus-shortcuts 1\nrender_resolution 1920x1440\nrenderer brender-compute\n"})
    {
        CHECK(SDL_SaveFile(path.u8string().c_str(), legacy, std::strlen(legacy)));
        shortcuts_defaults(&loaded);
        CHECK(shortcuts_load(path.u8string().c_str(), &loaded, message, sizeof(message)));
        CHECK(loaded.renderer == MARCUS_RENDERER_BRENDER_COMPUTE && loaded.work_renderer == loaded.renderer);
        CHECK(!std::strcmp(marcus_render_resolution(loaded.render_resolution)->name, "1920x1080") &&
              loaded.work_render_resolution == loaded.render_resolution);
    }
    const char independent[] = "marcus-shortcuts 1\nwork_renderer sdl-gpu\nwork_render_resolution 320x240\n"
                               "renderer brender-compute\nrender_resolution 1920x1440\n";
    CHECK(SDL_SaveFile(path.u8string().c_str(), independent, sizeof(independent) - 1));
    shortcuts_defaults(&loaded);
    CHECK(shortcuts_load(path.u8string().c_str(), &loaded, message, sizeof(message)));
    CHECK(loaded.renderer == MARCUS_RENDERER_BRENDER_COMPUTE && loaded.work_renderer == MARCUS_RENDERER_SDL_GPU);
    CHECK(!std::strcmp(marcus_render_resolution(loaded.render_resolution)->name, "1920x1080") &&
          !std::strcmp(marcus_render_resolution(loaded.work_render_resolution)->name, "320x180"));
    const char *legacyResolutions[] = {"320x240", "640x480", "960x720", "1024x768", "1600x1200",
                                       "1920x1440", "2560x1920", "2880x2160", "3840x2880", "4096x3072"};
    for (int index = 0; index < 10; ++index)
    {
        char legacy[256];
        SDL_snprintf(legacy, sizeof(legacy), "marcus-shortcuts 1\nrender_resolution %s\nwork_render_resolution %s\n",
                     legacyResolutions[index], legacyResolutions[9 - index]);
        CHECK(SDL_SaveFile(path.u8string().c_str(), legacy, std::strlen(legacy)));
        shortcuts_defaults(&loaded);
        CHECK(shortcuts_load(path.u8string().c_str(), &loaded, message, sizeof(message)));
        CHECK(loaded.render_resolution == index + 5 && loaded.work_render_resolution == 14 - index);
        CHECK(shortcuts_save(path.u8string().c_str(), &loaded, message, sizeof(message)));
        auto saved = static_cast<char *>(SDL_LoadFile(path.u8string().c_str(), nullptr));
        CHECK(saved && std::strstr(saved, marcus_render_resolution(loaded.render_resolution)->name) &&
              std::strstr(saved, marcus_render_resolution(loaded.work_render_resolution)->name));
        SDL_free(saved);
    }
    const char oldSettings[] = "marcus-shortcuts 1\npause_on_focus_loss 1\n";
    CHECK(SDL_SaveFile(path.u8string().c_str(), oldSettings, sizeof(oldSettings) - 1));
    shortcuts_defaults(&loaded);
    CHECK(shortcuts_load(path.u8string().c_str(), &loaded, message, sizeof(message)));
    CHECK(loaded.renderer == MARCUS_RENDERER_BRENDER && loaded.pause_on_focus_loss);
    CHECK(loaded.render_resolution == MARCUS_DEFAULT_RESOLUTION);
    CHECK(loaded.work_renderer == MARCUS_RENDERER_BRENDER && loaded.work_render_resolution == MARCUS_DEFAULT_RESOLUTION);
    for (const char *invalid :
         {"marcus-shortcuts 1\nrenderer unknown\n", "marcus-shortcuts 1\nrenderer sdl-gpu\nrenderer brender\n",
          "marcus-shortcuts 1\nrender_resolution 1920x1081\n",
          "marcus-shortcuts 1\nrender_resolution 320x240\nrender_resolution 640x480\n",
          "marcus-shortcuts 1\nrender_resolution 640x480 extra\n",
          "marcus-shortcuts 1\nwork_renderer unknown\n",
          "marcus-shortcuts 1\nwork_renderer sdl-gpu\nwork_renderer brender\n",
          "marcus-shortcuts 1\nwork_render_resolution 1920x1081\n",
          "marcus-shortcuts 1\nwork_render_resolution 320x240\nwork_render_resolution 640x480\n",
          "marcus-shortcuts 1\nwork_render_resolution 640x480 extra\n"})
    {
        CHECK(SDL_SaveFile(path.u8string().c_str(), invalid, std::strlen(invalid)));
        CHECK(!shortcuts_load(path.u8string().c_str(), &loaded, message, sizeof(message)));
        CHECK(loaded.renderer == MARCUS_RENDERER_BRENDER && loaded.pause_on_focus_loss);
        CHECK(loaded.render_resolution == MARCUS_DEFAULT_RESOLUTION);
        CHECK(loaded.work_renderer == MARCUS_RENDERER_BRENDER && loaded.work_render_resolution == MARCUS_DEFAULT_RESOLUTION);
    }

    constexpr int size = 64, stride = 72;
    std::vector<unsigned char> colour(stride * size), depth(stride * size * 2);
    const auto reset = [&] {
        std::fill(colour.begin(), colour.end(), 9);
        std::fill(depth.begin(), depth.end(), 255);
        const Uint16 front = 500;
        std::memcpy(depth.data() + (20 * stride + 21) * 2, &front, 2);
    };
    const auto zAt = [&](int x, int y) {
        Uint16 value;
        std::memcpy(&value, depth.data() + (y * stride + x) * 2, 2);
        return value;
    };
    BrRasterTriangle triangle = {};
    triangle.vertices[0] = {10, 10, 10000, 1, 0, 0, 37};
    triangle.vertices[1] = {54, 10, 10000, 1, 0, 0, 37};
    triangle.vertices[2] = {10, 54, 10000, 1, 0, 0, 37};
    int unavailable = 0;
    std::vector<unsigned char> reference;
    for (bool compute : {true, false})
    {
        auto renderer = MarcusCreateGPURenderer(compute, error);
        if (!renderer)
        {
            std::fprintf(stderr, "%s unavailable: %s\n", compute ? "Compute" : "Graphics", error.message.c_str());
            ++unavailable;
            continue;
        }
        reset();
        CHECK(renderer->Begin(size, size, error));
        renderer->Triangle(triangle);
        CHECK(renderer->End(colour.data(), stride, depth.data(), stride * 2, error));
        CHECK(colour[20 * stride + 20] == 37 && zAt(20, 20) == 10000);
        CHECK(colour[20 * stride + 21] == 9 && zAt(21, 20) == 500);
        CHECK(colour[2 * stride + 2] == 9 && zAt(2, 2) == 65535);
        CHECK(colour[64] == 9); // Do not overwrite row padding.
        if (compute)
            reference = colour;
        else if (!reference.empty())
        {
            int differences = 0;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    if (reference[y * stride + x] != colour[y * stride + x] && differences++ < 8)
                        std::fprintf(stderr, "edge difference at %d,%d: compute %u graphics %u\n", x, y,
                                     reference[y * stride + x], colour[y * stride + x]);
            CHECK(reference == colour);
        }
        // Ordered equal-depth overdraw, zero-key transparency, then opaque zero.
        unsigned char texels[] = {0, 65, 91, 123};
        auto textured = triangle;
        textured.texture = {texels, 2, 2, 2};
        CHECK(renderer->Begin(size, size, error));
        renderer->Triangle(textured);
        reset();
        CHECK(renderer->End(colour.data(), stride, depth.data(), stride * 2, error));
        CHECK(colour[20 * stride + 20] == 9 && zAt(20, 20) == 65535);
        textured.flags = 0x10000 | 0x20;
        CHECK(renderer->Begin(size, size, error));
        renderer->Triangle(textured);
        CHECK(renderer->End(colour.data(), stride, depth.data(), stride * 2, error));
        CHECK(colour[20 * stride + 20] == 0 && zAt(20, 20) == 10000);
        // A pitched map, negative wrapping coordinates, and a shade lookup.
        unsigned char pitched[] = {2, 1, 255, 1, 2, 255};
        unsigned char shades[] = {10, 11, 12, 20, 21, 22};
        textured.texture = {pitched, 2, 2, 3};
        textured.shade = {shades, 3, 2, 3};
        textured.flags = 1;
        for (auto &v : textured.vertices)
        {
            v.u = -2;
            v.v = 0;
            v.intensity = 1;
        }
        reset();
        CHECK(renderer->Begin(size, size, error));
        renderer->Triangle(textured);
        auto near = triangle;
        for (auto &v : near.vertices)
        {
            v.z = 9000;
            v.intensity = 42;
        }
        renderer->Triangle(near);
        renderer->Triangle(textured);
        CHECK(renderer->End(colour.data(), stride, depth.data(), stride * 2, error));
        CHECK(colour[20 * stride + 20] == 42 && zAt(20, 20) == 9000);
        reset();
        CHECK(renderer->Begin(size, size, error));
        renderer->Triangle(textured);
        CHECK(renderer->End(colour.data(), stride, depth.data(), stride * 2, error));
        CHECK(colour[20 * stride + 20] == 22);
        // Equal-depth triangles retain submission order.
        reset();
        CHECK(renderer->Begin(size, size, error));
        renderer->Triangle(triangle);
        auto equal = triangle;
        for (auto &v : equal.vertices)
            v.intensity = 73;
        renderer->Triangle(equal);
        CHECK(renderer->End(colour.data(), stride, depth.data(), stride * 2, error));
        CHECK(colour[20 * stride + 20] == 73 && zAt(20, 20) == 10000);
        // Analytic perspective UV: at this sample affine U=1.43 (texel 1)
        // while interpolating U/W and 1/W gives U=2.35 (wrapped texel 0).
        textured = triangle;
        textured.texture = {pitched, 2, 2, 3};
        textured.flags = 0x10000 | 0x20;
        textured.vertices[0].w = 4;
        textured.vertices[1].u = 6;
        reset();
        CHECK(renderer->Begin(size, size, error));
        renderer->Triangle(textured);
        CHECK(renderer->End(colour.data(), stride, depth.data(), stride * 2, error));
        CHECK(colour[20 * stride + 20] == 2);
        // Repeated empty frames must discard the previous frame's geometry.
        reset();
        CHECK(renderer->Begin(size, size, error));
        CHECK(renderer->End(colour.data(), stride, depth.data(), stride * 2, error));
        CHECK(colour[20 * stride + 20] == 9 && zAt(20, 20) == 65535);
    }
    if (unavailable)
        return unavailable == 2 ? 77 : 1;

    // Optional content integration, without a window or UI input.
    if (argc == 4)
    {
        unsigned sourceWidth = 0, sourceHeight = 0;
        {
            // Inspect the actual raster size and seeded background/depth,
            // independently of the size of the returned RGBA image.
            struct ResolutionProbe final : ThreeMM::PreviewRasterizer
            {
                unsigned width = 0, height = 0;
                bool Begin(unsigned w, unsigned h, ThreeMM::Error &) override
                {
                    width = w; height = h;
                    return true;
                }
                void Triangle(const BrRasterTriangle &) noexcept override {}
                bool End(unsigned char *, int, unsigned char *, int, ThreeMM::Error &) override { return true; }
            } probe;
            ThreeMM::Preview preview;
            const auto backgrounds = std::filesystem::u8path(argv[3]) / "bkgds.3cn";
            CHECK(preview.Open(backgrounds.u8string(), argv[2], error));
            CHECK(preview.Select({0x424b4744, 0}, error));
            ThreeMM::PreviewView view;
            view.includeDepth = true;
            view.rasterizer = &probe;
            ThreeMM::PreviewImage native, high;
            CHECK(preview.Render(view, native, error));
            sourceWidth = probe.width; sourceHeight = probe.height;
            for (unsigned padding : {0u, 24u})
            {
                view.width = sourceWidth * 2;
                view.height = sourceHeight * 2 + padding;
                view.renderAtOutputResolution = false;
                CHECK(preview.Render(view, native, error));
                CHECK(probe.width == sourceWidth && probe.height == sourceHeight);
                view.renderAtOutputResolution = true;
                CHECK(preview.Render(view, high, error));
                CHECK(probe.width == sourceWidth * 2 && probe.height == sourceHeight * 2);
                CHECK(high.rgba == native.rgba && high.depth == native.depth);
            }
        }
        player = marcus_create();
        CHECK(marcus_open(player, argv[1], argv[2], argv[3]));
        MarcusImage image = {};
        MarcusInfo before = {}, after = {};
        CHECK(marcus_seek(player, 10) && marcus_info(player, &before));
        CHECK(marcus_render(player, sourceWidth, sourceHeight, 1, 0, 0, 1, &image));
        const std::vector<unsigned char> native(image.rgba, image.rgba + image.pitch * image.height);
        CHECK(marcus_render(player, sourceWidth * 2, sourceHeight * 2, 1, 0, 0, 1, &image));
        unsigned detailed = 0;
        for (unsigned y = 0; y < sourceHeight * 2; ++y)
            for (unsigned x = 0; x < sourceWidth * 2; ++x)
                detailed += std::memcmp(image.rgba + y * image.pitch + x * 4,
                                        native.data() + ((y / 2) * sourceWidth + x / 2) * 4, 4) != 0;
        CHECK(detailed > 0); // Actors must be rerasterized, not enlarged with the background.
        CHECK(marcus_render(player, 320, 240, 1, 0, 0, 1, &image));
        const std::vector<unsigned char> cpu(image.rgba, image.rgba + image.pitch * image.height);
        for (int backend : {MARCUS_RENDERER_BRENDER_COMPUTE, MARCUS_RENDERER_SDL_GPU, MARCUS_RENDERER_BRENDER})
        {
            CHECK(marcus_set_renderer(player, backend));
            CHECK(marcus_get_renderer(player) == backend && marcus_info(player, &after));
            CHECK(before.scene == after.scene && before.frame == after.frame && before.camera == after.camera);
            CHECK(!marcus_set_renderer(player, -1) && marcus_get_renderer(player) == backend);
            for (int index = 0; index < MARCUS_RESOLUTION_COUNT; ++index)
            {
                const auto *resolution = marcus_render_resolution(index);
                for (int authored : {1, 0})
                {
                    CHECK(marcus_render(player, resolution->width, resolution->height, authored, -18, 12, 1, &image));
                    CHECK(image.width == resolution->width && image.height == resolution->height);
                    MarcusCamera camera = image.camera;
                    CHECK(marcus_render_fly(player, resolution->width, resolution->height, &camera, &image));
                    CHECK(image.width == resolution->width && image.height == resolution->height);
                    CHECK(marcus_match_camera(player, resolution->width, resolution->height, &camera));
                }
            }
            // The shared limit must also work for a square render, picking and
            // camera matching, while larger requests fail before allocation.
            const int maximum = ThreeMM::PreviewMaxDimension;
            CHECK(marcus_render(player, maximum, maximum, 0, -18, 12, 1, &image));
            CHECK(image.width == maximum && image.height == maximum);
            int picked = -1;
            CHECK(marcus_pick(player, &image, maximum / 2, maximum / 2, &picked));
            MarcusCamera camera;
            CHECK(marcus_match_camera(player, maximum, maximum, &camera));
            CHECK(!marcus_match_camera(player, maximum + 1, maximum, &camera));
            CHECK(!marcus_render(player, maximum + 1, maximum, 0, 0, 0, 1, &image) && !image.rgba);
            CHECK(!marcus_render(player, maximum, maximum + 1, 1, 0, 0, 1, &image) && !image.rgba);
        }
        CHECK(marcus_render(player, 320, 240, 1, 0, 0, 1, &image));
        CHECK(!std::memcmp(cpu.data(), image.rgba, cpu.size()));
        // Two camera profiles alternate backends/sizes without recreating GPU
        // devices. Even an unavailable driver hint cannot break cached devices.
        SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, "marcus-unavailable-driver", SDL_HINT_OVERRIDE);
        for (int repeat = 0; repeat < 3; ++repeat)
            for (int backend : {MARCUS_RENDERER_BRENDER_COMPUTE, MARCUS_RENDERER_SDL_GPU})
            {
                CHECK(marcus_set_renderer(player, backend));
                CHECK(marcus_render(player, 640, 480, 1, 0, 0, 1, &image)); // Matched work camera.
                CHECK(image.width == 640 && image.height == 480);
                MarcusCamera camera = image.camera;
                CHECK(marcus_render_fly(player, 640, 480, &camera, &image));
                CHECK(image.width == 640 && image.height == 480);
                CHECK(marcus_set_renderer(player, MARCUS_RENDERER_BRENDER));
                CHECK(marcus_render(player, 320, 240, 1, 0, 0, 1, &image)); // Movie camera.
                CHECK(!std::memcmp(cpu.data(), image.rgba, cpu.size()));
                CHECK(marcus_info(player, &after) && before.scene == after.scene && before.frame == after.frame &&
                      before.camera == after.camera);
            }
        marcus_destroy(player);
        // A new device's initialization failure retains the live renderer/movie.
        player = marcus_create();
        CHECK(marcus_open(player, argv[1], argv[2], argv[3]) && marcus_seek(player, 10));
        CHECK(!marcus_set_renderer(player, MARCUS_RENDERER_SDL_GPU));
        SDL_ResetHint(SDL_HINT_GPU_DRIVER);
        CHECK(marcus_get_renderer(player) == MARCUS_RENDERER_BRENDER);
        CHECK(marcus_render(player, 320, 240, 1, 0, 0, 1, &image));
        CHECK(!std::memcmp(cpu.data(), image.rgba, cpu.size()));
        marcus_destroy(player);
    }
    std::puts("Renderer checks passed (compute and graphics).");
    return 0;
}
