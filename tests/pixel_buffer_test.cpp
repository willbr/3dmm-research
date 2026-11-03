/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pixel_buffer.h"
#include <cstdio>
#include <cstring>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Pixel storage must not include native bitmap headers
#endif
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "Pixel buffer check failed at %d\n", __LINE__);                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    using namespace KauaiPixels;
    const auto baseline = PixelBuffers();
    {
        PixelBuffer buffer;
        CHECK(!buffer.Pixels() && !buffer.Width() && !buffer.Pitch() && !buffer.Bytes());
        struct Layout
        {
            int depth, pitch;
        } layouts[] = {{1, 8}, {4, 20}, {8, 36}, {16, 68}, {24, 100}, {32, 132}};
        for (auto layout : layouts)
        {
            CHECK(buffer.Init(33, 3, layout.depth));
            CHECK(buffer.Width() == 33 && buffer.Height() == 3 && buffer.Depth() == layout.depth &&
                  buffer.Pitch() == layout.pitch && buffer.Bytes() == std::size_t(layout.pitch * 3));
            const auto bytes = static_cast<unsigned char *>(buffer.Pixels());
            for (std::size_t i = 0; i < buffer.Bytes(); ++i)
                CHECK(bytes[i] == 0);
            CHECK(reinterpret_cast<std::uintptr_t>(buffer.Pixels()) % alignof(std::uint32_t) == 0);
            std::memset(bytes, 0x5A, buffer.Bytes());
            for (auto invalid : {0, 2, 3, 12, 64})
                CHECK(!buffer.Init(33, 3, invalid) && buffer.Pixels() == bytes && buffer.Pitch() == layout.pitch);
            CHECK(!buffer.Init(0, 3, layout.depth) && !buffer.Init(33, -1, layout.depth));
            CHECK(!buffer.Init(INT32_MAX, 1, 32) && !buffer.Init(1, INT32_MAX, 1));
            for (std::size_t i = 0; i < buffer.Bytes(); ++i)
                CHECK(bytes[i] == 0x5A);
            const auto stats = PixelBuffers();
            CHECK(stats.liveBuffers == baseline.liveBuffers + 1 &&
                  stats.liveBytes == baseline.liveBytes + buffer.Bytes());
        }
        PixelBuffer independent;
        CHECK(independent.Init(3, 2, 8) && independent.Pitch() == 4 && independent.Bytes() == 8);
        CHECK(independent.Pixels() != buffer.Pixels());
        std::memset(independent.Pixels(), 7, independent.Bytes());
        CHECK(static_cast<const unsigned char *>(buffer.Pixels())[0] == 0x5A);
        independent.Reset();
        independent.Reset();
        CHECK(!independent.Pixels() && !independent.Depth() && !independent.Bytes());
    }
    CHECK(PixelBuffers().liveBuffers == baseline.liveBuffers && PixelBuffers().liveBytes == baseline.liveBytes &&
          PixelBuffers().allocations == baseline.allocations + 7);
    std::puts("Portable pixel storage: packed layouts, row alignment, initialization, transactional failure and "
              "ownership passed.");
    return 0;
}
