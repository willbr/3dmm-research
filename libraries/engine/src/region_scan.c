/* Scan-band policy converted from Kauai region.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/region_scan.h"

static MMI32 add_clamped(MMI32 a, MMI32 b)
{
    if (b > 0 && a > MM_REGION_END - b)
        return MM_REGION_END;
    if (b < 0 && a < (-MM_REGION_END - 1) - b)
        return -MM_REGION_END - 1;
    return a + b;
}

static MMI32 difference_clamped(MMI32 a, MMI32 b)
{
    if (b < 0 && a > MM_REGION_END + b)
        return MM_REGION_END;
    if (b > 0 && a < (-MM_REGION_END - 1) + b)
        return -MM_REGION_END - 1;
    return a - b;
}

static MMI32 edge_x(const MMRegionScan *scan, size_t index)
{
    return add_clamped(scan->bias, scan->words[index]);
}

static void scan_band(MMRegionScan *scan)
{
    size_t first, end, last;
    scan->first_edge = scan->end_edge = 0;
    scan->first = MM_REGION_END;
    while (scan->rows_left > 0 && scan->next_band < scan->count)
    {
        scan->rows = add_clamped(scan->rows, scan->words[scan->next_band++]);
        first = end = scan->next_band;
        while (end < scan->count && scan->words[end] != MM_REGION_END)
            ++end;
        scan->next_band = end < scan->count ? end + 1 : end;
        if (scan->rows <= 0)
            continue;

        while (first + 1 < end && edge_x(scan, first + 1) <= 0)
            first += 2;
        last = first;
        while (last + 1 < end && edge_x(scan, last) < scan->width)
            last += 2;
        if (first < last && scan->width >= 0)
        {
            scan->first = edge_x(scan, first);
            if (scan->first < 0)
                scan->first = 0;
            scan->first_edge = first + 1;
            scan->end_edge = last;
        }
        return;
    }
    scan->next_band = scan->count;
    scan->rows = MM_REGION_END_HEIGHT;
}

void mm_region_scan_init(MMRegionScan *scan, const MMPixelRect *bounds, MMI32 offset,
                         const MMRegionWord *bands, size_t count, const MMPixelRect *clip)
{
    MMPixelRect area = *bounds;
    if (!bands)
    {
        mm_pixel_rect_intersect(&area, clip);
        if (area.left >= area.right || area.top >= area.bottom)
            area.left = area.top = area.right = area.bottom = 0;
        scan->rectangle[0] = difference_clamped(area.bottom, area.top);
        scan->rectangle[1] = 0;
        scan->rectangle[2] = difference_clamped(area.right, area.left);
        scan->rectangle[3] = MM_REGION_END;
        bands = scan->rectangle;
        count = 4;
        offset = 0;
    }
    scan->words = bands;
    scan->count = count;
    scan->next_band = scan->first_edge = scan->end_edge = 0;
    scan->first = MM_REGION_END;
    scan->bias = difference_clamped(add_clamped(area.left, offset), clip->left);
    scan->width = difference_clamped(clip->right, clip->left);
    scan->rows = difference_clamped(area.top, clip->top);
    scan->rows_left = difference_clamped(clip->bottom, clip->top);
    mm_region_scan_advance(scan, 0);
}

void mm_region_scan_advance(MMRegionScan *scan, MMI32 rows)
{
    scan->rows_left = difference_clamped(scan->rows_left, rows);
    scan->rows = difference_clamped(scan->rows, rows);
    if (scan->rows <= 0)
        scan_band(scan);
    scan->edge = scan->first_edge;
    scan->x = scan->first;
    scan->opens = 1;
}

MMI32 mm_region_scan_fetch(MMRegionScan *scan)
{
    if (scan->edge < scan->end_edge)
    {
        scan->x = edge_x(scan, scan->edge++);
        if (scan->x > scan->width)
            scan->x = scan->width;
        scan->opens = !scan->opens;
    }
    else
    {
        scan->x = MM_REGION_END;
        scan->opens = 1;
    }
    return scan->x;
}

MMI32 mm_region_scan_edge_count(const MMRegionScan *scan)
{
    return scan->first == MM_REGION_END ? 0 : (MMI32)(scan->end_edge - scan->first_edge + 1);
}

int mm_region_scan_pair(MMRegionScan *scan, MMI32 *left, MMI32 *right)
{
    if (scan->x == MM_REGION_END)
        return 0;
    *left = scan->x;
    *right = mm_region_scan_fetch(scan);
    mm_region_scan_fetch(scan);
    return 1;
}

int mm_region_combine(MMRegionOperation operation, MMRegionScan *left, MMRegionScan *right,
                      MMI32 rows, const MMRegionBandSink *sink)
{
    MMI32 height, x;
    unsigned inside, previous;
    while (rows > 0)
    {
        height = left->rows < right->rows ? left->rows : right->rows;
        if (height > rows)
            height = rows;
        if (!sink->start(sink->context, height, add_clamped(mm_region_scan_edge_count(left),
                                                          mm_region_scan_edge_count(right))))
            return 0;
        inside = 0;
        while (left->x != MM_REGION_END || right->x != MM_REGION_END)
        {
            x = left->x < right->x ? left->x : right->x;
            previous = inside;
            if (left->x == x)
            {
                inside ^= 1;
                mm_region_scan_fetch(left);
            }
            if (right->x == x)
            {
                inside ^= 2;
                mm_region_scan_fetch(right);
            }
            if (((operation >> previous) ^ (operation >> inside)) & 1)
                sink->edge(sink->context, x);
        }
        sink->end(sink->context);
        rows -= height;
        if (rows > 0)
        {
            mm_region_scan_advance(left, height);
            mm_region_scan_advance(right, height);
        }
    }
    return 1;
}
