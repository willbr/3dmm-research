#include "asm_renderer.h"
#include "player.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "ASM check failed at %d: %s\n", __LINE__, marcus_error(player.get()));                \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv)
{
    ThreeMM::Error error;
    if (argc != 4 || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(argv[2]) ||
        !MarcusASMRenderer::Available(error))
        return 77;
    std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)> player(marcus_create(), marcus_destroy);
    CHECK(player && marcus_open(player.get(), argv[1], argv[2], argv[3]));
    marcus_set_muted(player.get(), 1);
    MarcusInfo info = {};
    MarcusScene scene = {};
    MarcusImage image = {};
    CHECK(marcus_info(player.get(), &info));
    for (int selected = 0; selected < info.scene_count && selected < 3; ++selected)
    {
        CHECK(marcus_scene(player.get(), selected, &scene) && marcus_select_scene(player.get(), selected));
        for (long frame : {scene.first_frame, scene.last_frame})
        {
            CHECK(marcus_seek(player.get(), frame));
            CHECK(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER));
            CHECK(marcus_render(player.get(), 544, 306, 1, 0, 0, 1, &image));
            const std::vector<unsigned char> cpu(image.rgba, image.rgba + image.pitch * image.height);
            CHECK(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER_ASM));
            CHECK(marcus_render(player.get(), 544, 306, 1, 0, 0, 1, &image));
            CHECK(image.width == 544 && image.height == 306 && image.scene == selected && image.frame == frame);
            const std::vector<unsigned char> assembly(image.rgba, image.rgba + image.pitch * image.height);
            CHECK(marcus_render(player.get(), 544, 306, 1, 0, 0, 1, &image));
            CHECK(!std::memcmp(assembly.data(), image.rgba, assembly.size()));
            CHECK(marcus_render(player.get(), 320, 240, 0, 15, 10, 1, &image));
            CHECK(image.width == 320 && image.height == 240 && image.projection.valid);
            CHECK(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER));
            CHECK(marcus_render(player.get(), 544, 306, 1, 0, 0, 1, &image));
            CHECK(!std::memcmp(cpu.data(), image.rgba, cpu.size()));
        }
    }
    CHECK(marcus_open(player.get(), argv[1], argv[2], argv[3]));
    CHECK(marcus_set_renderer(player.get(), MARCUS_RENDERER_BRENDER_ASM));
    CHECK(marcus_render(player.get(), 544, 306, 1, 0, 0, 1, &image));
    std::puts("x86 ASM scene/frame switching, deterministic pixels, work camera, CPU restoration and reload passed");
}
