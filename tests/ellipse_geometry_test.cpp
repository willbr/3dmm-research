/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "ellipse_geometry.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WINDOWS_
#error Ellipse geometry must not import the Windows SDK
#endif
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Ellipse geometry failed: %d\n", __LINE__);                                           \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
int main()
{
    using KauaiPixels::EllipseSpan;
    std::vector<EllipseSpan> rows;
    const auto check = [&](int width, int height, std::initializer_list<EllipseSpan> expected) {
        CHECK(KauaiPixels::EllipseRows(width, height, 0, height, rows) && rows.size() == expected.size());
        unsigned y = 0;
        for (const auto &span : expected)
        {
            CHECK(rows[y].left == span.left && rows[y].right == span.right);
            ++y;
        }
    };
    check(1, 1, {{0, 1}});
    check(2, 2, {{0, 0}, {0, 0}});
    check(3, 3, {{1, 2}, {0, 3}, {1, 2}});
    check(4, 4, {{1, 3}, {0, 4}, {0, 4}, {1, 3}});
    check(5, 5, {{2, 3}, {1, 4}, {0, 5}, {1, 4}, {2, 3}});
    check(4, 10, {{0, 0}, {1, 3}, {1, 3}, {0, 4}, {0, 4}, {0, 4}, {0, 4}, {1, 3}, {1, 3}, {0, 0}});
    CHECK(KauaiPixels::EllipseRows(3, 3, -2, 7, rows));
    CHECK(rows.size() == 7 && !rows[0].right && !rows[1].right && rows[2].left == 1 && rows[2].right == 2 &&
          rows[3].left == 0 && rows[3].right == 3 && !rows[5].right && !rows[6].right);
    CHECK(!KauaiPixels::EllipseRows(-1, 3, 0, 3, rows) && rows.size() == 7);
    CHECK(!KauaiPixels::EllipseRows(INT32_MAX, 3, 0, 3, rows) && rows.size() == 7);
    CHECK(!KauaiPixels::EllipseRows(3, 3, 0, INT32_MAX, rows) && rows.size() == 7);
    CHECK(KauaiPixels::EllipseRows(0, 3, 0, 3, rows) && rows.size() == 3 && !rows[1].right);
    CHECK(KauaiPixels::EllipseRows(2049, 2049, 1024, 1, rows) && rows.size() == 1 && rows[0].left == 0 &&
          rows[0].right == 2049);
    std::puts("Portable ellipse geometry: exact small/degenerate fills, narrow ovals, clipped row ranges, large-bound "
              "path and rejected extents passed.");
}
