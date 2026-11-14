#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include "gpu_renderer.h"
#include "zb_capture.h"
#include "three_mm/preview.h"
#include "soc_core.h"
#pragma pack(push, 4)
#include "zb.h"
#pragma pack(pop)
#undef size
ASSERTNAME

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "Raster parity check failed at %d: %s\n", __LINE__, #value); std::exit(1); } } while (0)

int main()
{
    BrBegin();
    BrZbBegin(BR_PMT_INDEX_8, BR_PMT_DEPTH_16);
    constexpr unsigned width = 64, height = 48, stride = 72;
    std::vector<unsigned char> cpu(stride * height), gpu(cpu.size());
    std::vector<std::uint16_t> cpuDepth(cpu.size()), gpuDepth(cpu.size());
    auto target = BrPixelmapAllocate(BR_PMT_INDEX_8, width, height, cpu.data(), BR_PMAF_NORMAL);
    CHECK(target);
    target->row_bytes = stride;
    fw.output = target;
    unsigned char texture[11 * 5] = {}, shade[256 * 256], palette[1024];
    for (unsigned y = 0; y < 5; ++y)
        for (unsigned x = 0; x < 7; ++x) texture[y * 11 + x] = (x + 3 * y) % 4 ? 17 + x + 7 * y : 0;
    for (unsigned i = 0; i < 256; ++i)
    {
        palette[i * 4] = palette[i * 4 + 1] = palette[i * 4 + 2] = static_cast<unsigned char>(i);
        palette[i * 4 + 3] = 255;
        for (unsigned texel = 0; texel < 256; ++texel) shade[i * 256 + texel] = (texel + i * 7) & 255;
    }
    br_pixelmap map = {};
    map.width = 7; map.height = 5; map.row_bytes = 11; map.pixels = texture;
    br_material material = {};
    material.colour_map = &map;
    std::uint32_t random = 0x52f36a19;
    const auto next = [&]() { return random = random * 1664525u + 1013904223u; };
    std::vector<std::vector<BrRasterTriangle>> cases;
    for (unsigned sample = 0; sample < 60; ++sample)
    {
        BrRasterTriangle triangle = {};
        for (unsigned i = 0; i < 3; ++i)
        {
            auto &v = triangle.fixed_vertices[i];
            v.x = std::int32_t((2 + next() % 60) * 65536u + (next() & 65535));
            v.y = std::int32_t((2 + next() % 44) * 65536u + (next() & 65535));
            v.z = std::int32_t((5000 + next() % 20000) * 65536u + (next() & 65535));
            v.intensity = std::int32_t((next() % 200 + 20) * 65536u + (next() & 65535));
            v.u = std::int32_t((next() % 50) * 65536u + (next() & 65535)) - 25 * 65536;
            v.v = std::int32_t((next() % 34) * 65536u + (next() & 65535)) - 17 * 65536;
        }
        if (sample % 4 == 0) triangle.fixed_vertices[1].y = triangle.fixed_vertices[0].y;
        if (sample % 4 == 1) triangle.fixed_vertices[2].y = triangle.fixed_vertices[1].y;
        // Native ASM receives clipped vertices. Include exact viewport edges
        // without feeding it out-of-buffer coordinates that only C clips.
        if (sample % 8 == 2)
        {
            triangle.fixed_vertices[0].x = 0;
            triangle.fixed_vertices[1].x = width * 65536;
            triangle.fixed_vertices[2].y = height * 65536;
        }
        std::vector<BrRasterTriangle> batch;
        for (unsigned layer = 0; layer < 4; ++layer)
        {
            auto copy = triangle;
            const auto kind = (sample + layer) % 3;
            copy.raster_mode = kind ? 2 : 1;
            copy.flags = 1 | (kind == 2 ? 0x10000 : 0);
            if (kind) { copy.texture = {texture, 7, 5, 11}; copy.shade = {shade, 256, 256, 256}; }
            for (unsigned i = 0; i < 3; ++i)
            {
                auto &v = copy.fixed_vertices[i];
                if (layer == 1) v.z -= 65536; // Closer, then exact-depth overlap.
                v.intensity = (v.intensity + int(layer * 13 * 65536)) % (256 * 65536);
                copy.vertices[i] = {v.x / 65536.f, v.y / 65536.f, std::uint32_t(v.z) / 65536.f,
                                    1, v.u / 65536.f, v.v / 65536.f, v.intensity / 65536.f};
            }
            batch.push_back(copy);
        }
        cases.push_back(batch);
    }
    unsigned checked = 0, covered = 0;
    for (bool compute : {true, false})
    {
        ThreeMM::Error error;
        auto renderer = MarcusCreateGPURenderer(compute, error);
        if (!renderer) { std::fprintf(stderr, "%s\n", error.message.c_str()); continue; }
        ThreeMM::PreviewProjection projection;
        renderer->Frame(projection, palette, true);
        for (unsigned sample = 0; sample < cases.size(); ++sample)
        {
            std::fill(cpu.begin(), cpu.end(), 9);
            std::fill(cpuDepth.begin(), cpuDepth.end(), 65535);
            gpu = cpu; gpuDepth = cpuDepth;
            CHECK(renderer->Begin(width, height, error));
            for (const auto &triangle : cases[sample])
            {
                temp_vertex_fixed vertices[3] = {};
                for (unsigned i = 0; i < 3; ++i)
                {
                    const auto &v = triangle.fixed_vertices[i];
                    vertices[i].v[0] = v.x; vertices[i].v[1] = v.y; vertices[i].v[2] = v.z;
                    vertices[i].comp[C_I] = v.intensity;
                    vertices[i].comp[C_U] = v.u; vertices[i].comp[C_V] = v.v;
                }
                zb.colour_buffer = cpu.data(); zb.depth_buffer = reinterpret_cast<br_fixed_ls *>(cpuDepth.data());
                zb.row_width = stride; zb.depth_row_width = stride * 2;
                zb.material = &material; zb.texture_buffer = texture; zb.shade_table = shade;
                if (triangle.raster_mode == 1) TriangleRenderPIZ2I(vertices, vertices + 1, vertices + 2);
                else if (triangle.flags & 0x10000) TriangleRenderPIZ2TIANT(vertices, vertices + 1, vertices + 2);
                else TriangleRenderPIZ2TIA(vertices, vertices + 1, vertices + 2);
                renderer->Triangle(triangle);
            }
            CHECK(renderer->End(gpu.data(), stride, reinterpret_cast<unsigned char *>(gpuDepth.data()), stride * 2, error));
            unsigned differences = 0;
            for (unsigned i = 0; i < cpu.size(); ++i)
            {
                if (cpu[i] != gpu[i] || cpuDepth[i] != gpuDepth[i])
                {
                    if (differences++ < 3) std::fprintf(stderr, "%s case %u pixel %u,%u: CPU %u/%u GPU %u/%u\n",
                        compute ? "compute" : "graphics", sample, i % stride, i / stride,
                        cpu[i], cpuDepth[i], gpu[i], gpuDepth[i]);
                }
                covered += cpuDepth[i] != 65535;
            }
            CHECK(differences == 0);
            ++checked;
        }
    }
    BrPixelmapFree(target);
    BrZbEnd(); BrEnd();
    if (!checked) return 77;
    CHECK(checked == 120 && covered > 1000);
    std::puts("GPU/CPU scanline colour and depth parity passed (120 frames)");
}
