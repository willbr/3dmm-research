#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "bren.h"
#undef size
using namespace BRender;
ASSERTNAME
void FrameMain(void)
{
}
class TestApp : public ApplicationBase
{
  public:
#ifdef DEBUG
    bool FAssertProcApp(PSZS file, long line, PSZS message, void *, long) override
    {
        std::fprintf(stderr, "%s:%ld: %s\n", file, line, message);
        std::exit(1);
    }
#endif
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "world buffers: %d\n", __LINE__);                                                     \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static unsigned hash = 2166136261U, recorded;
static void Record(const void *p, size_t n)
{
    auto b = static_cast<const byte *>(p);
    for (size_t i = 0; i < n; ++i)
        hash = (hash ^ b[i]) * 16777619U;
    recorded += unsigned(n);
    if (snapshot)
        CHECK(std::fwrite(p, 1, n, snapshot) == n);
}
static void Value(LegacyLong n)
{
    Record(&n, 4);
}
struct Access : World
{
    static void State(PWorld w, bool pixels)
    {
        const auto &buffers = w->*&Access::_buffers;
        RC view(0, 0, buffers.width, buffers.height), buffer(0, 0, buffers.buffer_width, buffers.buffer_height);
        Record(&view, sizeof(view));
        Record(&buffer, sizeof(buffer));
        Value(w->FHalfX());
        Value(w->FHalfY());
        Value(w->RenderScale());
        Value((w->*&Access::_render).changed);
        const auto background = mm_world_background_pixels(&buffers);
        Value(background.pixels != nullptr);
        Value(buffers.owners[MM_WORLD_RGB_WORKING] != nullptr);
        // Preserve the snapshot field for the vertical-only half-resolution mode.
        Value(buffers.scale == 1 && !buffers.half_x && buffers.half_y);
        Value(buffers.owners[MM_WORLD_DEPTH_WORKING] != nullptr);
        Value(buffers.owners[MM_WORLD_DEPTH_BACKGROUND] != nullptr);
        (w->*&Access::_SyncPixelmaps)();
        for (auto field : {&Access::_bpmpRGB, &Access::_bpmpZ})
        {
            auto &b = w->*field;
            LegacyLong values[] = {b.type, b.row_bytes, b.width, b.height, b.origin_x, b.origin_y, b.pixels != nullptr};
            Record(values, sizeof(values));
        }
        for (auto region : {&buffers.dirty_working, &buffers.dirty_screen})
        {
            const auto &bounds = region->bounds;
            RC r(bounds.left, bounds.top, bounds.right, bounds.bottom);
            Value(mm_region_empty(region) != 0);
            Record(&r, sizeof(r));
        }
        if (pixels)
        {
            auto depth = mm_depth_owner_bitmap(static_cast<MMDepthOwner *>(buffers.owners[MM_WORLD_DEPTH_BACKGROUND]));
            Record(depth->pixels, depth->row_bytes * buffer.Dyp());
            Record(background.pixels, background.bytes);
        }
    }
};
int main()
{
    auto app = NewObj TestApp;
    CHECK(app);
    if (auto path = std::getenv("MM_WORLD_BUFFERS_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        CHECK(snapshot);
    }
#ifndef KAUAI_SDL3
    vwig.hdcApp = GetDC(nullptr);
    CHECK(vwig.hdcApp);
#endif
    CHECK(vntl.FInit());
    auto palette = DynamicArray::PglNew(sizeof(Color));
    CHECK(palette);
    for (int i = 0; i < 256; ++i)
    {
        Color c = {byte(i), byte(i), byte(i), 0};
        CHECK(palette->FAdd(&c));
    }
    GraphicsPort::SetActiveColors(palette, fpalIdentity);
    ReleasePpo(&palette);
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    auto cache = ChunkyResourceFile::PcrfNew(file, 1 << 20);
    CHECK(cache);
    byte pixels[16 * 12];
    for (int i = 0; i < 192; ++i)
        pixels[i] = byte(1 + (i * 13) % 254);
    RC r(0, 0, 16, 12);
    auto bitmap = NewMaskedBitmap(pixels, 16, 12, &r, 0, 0, 0);
    CHECK(bitmap);
    ChunkNumber rgb = 0, z = 0;
    DataBlock b;
    CHECK(file->FAdd(MaskedBitmapSize(bitmap), kctgMbmp, &rgb, &b) && WriteMaskedBitmap(bitmap, &b));
    b.Free();
    ReleaseMaskedBitmap(&bitmap);
    auto depth = NewDepthBitmap(16, 12);
    CHECK(depth);
    for (int i = 0; i < 384; ++i)
        static_cast<byte *>(mm_depth_owner_bitmap(depth)->pixels)[i] = byte(i * 7);
    CHECK(WriteDepthBitmap(depth, file->WriteOwnerIndex(), kctgZbmp, &z));
    mm_depth_owner_release(depth);
    for (int mode = 0; mode < 4; ++mode)
    {
        World::SetDefaultRenderScale(1);
        auto w = World::PbwldNew(16, 12, mode & 1, mode & 2);
        CHECK(w);
        Access::State(w, false);
        CHECK(w->FSetBackground(cache->CoreCache(), kctgMbmp, rgb, kctgZbmp, z));
        if (mode == 0)
        {
            auto core = cache->CoreCache();
            CHECK(core->count == 2);
            for (MMU32 i = 0; i < core->count; ++i)
            {
                MMResourceEntry entry;
                CHECK(mm_resource_cache_at(core, i, &entry) == MM_STORAGE_OK && entry.objects);
                const auto binding = entry.objects->state(entry.object);
                CHECK(binding->priority == crepTossFirst);
                CHECK(entry.objects == (binding->type == kctgMbmp ? mm_masked_owner_services()
                                                                 : mm_depth_owner_services()));
            }
        }
        Access::State(w, true);
        for (int change = 0; change < 4; ++change)
        {
            CHECK(w->FSetHalfMode(change & 1, change & 2));
            Access::State(w, true);
        }
        for (int scale : {2, 4, 1})
        {
            CHECK(w->FSetRenderScale(scale));
            Access::State(w, true);
        }
        CHECK(!w->FSetRenderScale(3));
        CHECK(!w->FSetRenderResolution(2, true));
        Access::State(w, true);
        CHECK(w->FSetRenderResolution(1, true));
        Access::State(w, true);
        CHECK(w->FSetRenderResolution(2, false));
        Access::State(w, true);
        ReleasePpo(&w);
    }
    ReleasePpo(&cache);
    ReleasePpo(&file);
    World::CloseBRender();
    World::SetDefaultRenderScale(1);
#ifndef KAUAI_SDL3
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#endif
    delete app;
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::printf("World buffers: bytes=%u fingerprint=%08X\n", recorded, hash);
    const unsigned short one = 1;
    if (*reinterpret_cast<const byte *>(&one))
        CHECK(recorded == 71376 && hash == 0x35BEFB80U);
}
