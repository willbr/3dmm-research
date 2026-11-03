/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "damage_map.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>

#ifdef _WINDOWS_
#error Damage maps must not depend on the Windows SDK
#endif
using namespace KauaiPixels;
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Damage map check failed: %d\n", __LINE__);                                           \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)

static bool Inside(const DamageRect &r, std::int64_t x, std::int64_t y)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
static void Verify(const DamageMap &map, const std::vector<unsigned char> &expected, int w, int h)
{
    std::vector<DamageRect> rectangles;
    CHECK(map.Snapshot(rectangles));
    std::vector<unsigned char> actual(w * h);
    for (const auto &r : rectangles)
    {
        CHECK(r.left >= 0 && r.top >= 0 && r.right <= w && r.bottom <= h);
        CHECK(r.left < r.right && r.top < r.bottom);
        for (int y = r.top; y < r.bottom; ++y)
            for (int x = r.left; x < r.right; ++x)
                CHECK(++actual[y * w + x] == 1);
    }
    CHECK(actual == expected);
    DamageRect bounds{w, h, 0, 0}, got;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (expected[y * w + x])
            {
                bounds.left = std::min(bounds.left, x);
                bounds.top = std::min(bounds.top, y);
                bounds.right = std::max(bounds.right, x + 1);
                bounds.bottom = std::max(bounds.bottom, y + 1);
            }
    const bool nonempty = bounds.right > bounds.left;
    if (!nonempty)
        bounds = {};
    CHECK(map.Bounds(got) == nonempty);
    CHECK(got.left == bounds.left && got.top == bounds.top && got.right == bounds.right && got.bottom == bounds.bottom);
}
int main()
{
    DamageMap map;
    DamageRect bounds;
    CHECK(!map.Bounds(bounds));
    std::uint32_t seed = 74291;
    const auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    for (int w : {1, 7, 64, 65, 131})
    {
        const int h = 9;
        CHECK(map.Init(w, h));
        std::vector<unsigned char> grid(w * h);
        for (int step = 0; step < 600; ++step)
        {
            DamageRect r{int(next() % (w + 20)) - 10, int(next() % 29) - 10, int(next() % (w + 20)) - 10,
                         int(next() % 29) - 10};
            if (step % 4)
            {
                if (r.left > r.right)
                    std::swap(r.left, r.right);
                if (r.top > r.bottom)
                    std::swap(r.top, r.bottom);
            }
            const auto operation = next() % 5;
            if (operation < 2)
            {
                const bool full = step % 17 == 0;
                if (full)
                    r = {0, 0, w, h};
                if (operation == 0)
                    map.Invalidate(full ? nullptr : &r);
                else
                    map.Validate(full ? nullptr : &r);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        if (Inside(r, x, y))
                            grid[y * w + x] = operation == 0;
            }
            else if (operation < 4)
            {
                const int dx = step % 11 ? int(next() % 19) - 9 : INT_MIN;
                const int dy = step % 13 ? int(next() % 19) - 9 : INT_MAX;
                CHECK(map.Scroll(r, dx, dy));
                auto old = grid;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        if (Inside(r, x, y))
                        {
                            const auto sx = std::int64_t(x) - dx, sy = std::int64_t(y) - dy;
                            grid[y * w + x] = Inside(r, sx, sy) && sx >= 0 && sx < w && sy >= 0 && sy < h
                                                  ? old[std::size_t(sy) * w + std::size_t(sx)]
                                                  : 0;
                        }
            }
            else
            {
                std::vector<DamageRect> taken;
                CHECK(map.Take(taken));
                std::fill(grid.begin(), grid.end(), 0);
                // A new repaint request after Take must survive the previous paint.
                map.Invalidate(&r);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        if (Inside(r, x, y))
                            grid[y * w + x] = 1;
            }
            Verify(map, grid, w, h);
        }
        CHECK(!map.Init(0, h) && !map.Init(w, -1) && !map.Init(INT_MAX, INT_MAX));
        Verify(map, grid, w, h);
        map.Invalidate();
        std::fill(grid.begin(), grid.end(), 1);
        Verify(map, grid, w, h);
    }
    std::puts("Damage maps: 3000 independent grid comparisons, word boundaries, scrolling, snapshots and repaint "
              "retention passed.");
}
