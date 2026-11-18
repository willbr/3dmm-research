/* Region ownership and geometry converted from Kauai region.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/region.h"
#include "mm/wide.h"
#include <string.h>

#define GROW_WORDS 100
#define MAX_WORDS ((MM_STORAGE_LIMIT - 1) / sizeof(MMRegionWord))

struct MMRegionBands
{
    MMCollectionStorage storage;
    size_t count, references;
};

typedef struct RegionBuilder
{
    MMRegionBands *bands;
    MMPixelRect reference, bounds;
    MMI32 y;
    size_t used, row, previous;
    int grow;
} RegionBuilder;

static const MMPixelRect empty = {0, 0, 0, 0};

static int rect_empty(const MMPixelRect *r)
{
    return r->left >= r->right || r->top >= r->bottom;
}

static int rect_equal(const MMPixelRect *a, const MMPixelRect *b)
{
    return (a->left == b->left && a->right == b->right && a->top == b->top && a->bottom == b->bottom) ||
           (rect_empty(a) && rect_empty(b));
}

static void rect_union(MMPixelRect *a, const MMPixelRect *b)
{
    if (rect_empty(b))
        return;
    if (rect_empty(a))
    {
        *a = *b;
        return;
    }
    if (a->left > b->left)
        a->left = b->left;
    if (a->top > b->top)
        a->top = b->top;
    if (a->right < b->right)
        a->right = b->right;
    if (a->bottom < b->bottom)
        a->bottom = b->bottom;
}

static MMRegionBands *bands_create(const MMStorageAllocator *allocator)
{
    MMCollectionStorage storage;
    MMRegionBands *bands;
    if (mm_storage_init(&storage, allocator) != MM_STORAGE_OK)
        return NULL;
    bands = storage.allocator.resize(storage.allocator.context, NULL, 0, sizeof(*bands), MM_STORAGE_NORMAL);
    if (bands)
    {
        bands->storage = storage;
        bands->storage.min_grow[0] = GROW_WORDS * sizeof(MMRegionWord);
        bands->count = 0;
        bands->references = 1;
    }
    return bands;
}

static void bands_release(MMRegionBands *bands)
{
    if (bands && !--bands->references)
    {
        MMStorageAllocator allocator = bands->storage.allocator;
        mm_storage_dispose(&bands->storage);
        allocator.release(allocator.context, bands, sizeof(*bands));
    }
}

const void *mm_region_band_data(const MMRegionBands *bands)
{
    return bands ? bands->storage.buffers[0].data : NULL;
}

static void scan_region(MMRegionScan *scan, const MMRegion *region, const MMPixelRect *clip)
{
    mm_region_scan_init(scan, &region->bounds, region->bias, mm_region_band_data(region->bands),
                        region->bands ? region->bands->count : 0, clip);
}

void mm_region_cursor_open(MMRegionCursor *cursor, const MMRegion *region, const MMPixelRect *clip)
{
    MMRegionBands *bands = region->bands;
    if (bands)
        ++bands->references;
    bands_release(cursor->bands);
    cursor->bands = bands;
    cursor->clip_left = clip->left;
    scan_region(&cursor->scan, region, clip);
}

void mm_region_cursor_close(MMRegionCursor *cursor)
{
    bands_release(cursor->bands);
    cursor->bands = NULL;
}

int mm_region_cursor_pair(MMRegionCursor *cursor, MMI32 *left, MMI32 *right)
{
    if (!mm_region_scan_pair(&cursor->scan, left, right))
        return 0;
    *left += cursor->clip_left;
    *right += cursor->clip_left;
    return 1;
}

int mm_region_visit_rectangles(const MMRegion *region, const MMPixelRect *clip,
                               MMRegionRectangleVisitor visit, void *context)
{
    MMRegionCursor cursor = {0};
    MMPixelRect rect;
    MMI32 width, height;
    int ok = 1;
    if (!mm_pixel_rect_extent(clip, &width, &height) || !width || !height)
        return 1;
    if (!region)
        return visit(context, clip);
    mm_region_cursor_open(&cursor, region, clip);
    for (rect.top = clip->top; rect.top < clip->bottom; rect.top = rect.bottom)
    {
        height = cursor.scan.rows;
        if (height > clip->bottom - rect.top)
            height = clip->bottom - rect.top;
        rect.bottom = rect.top + height;
        while (mm_region_cursor_pair(&cursor, &rect.left, &rect.right))
            if (!visit(context, &rect))
            {
                ok = 0;
                goto done;
            }
        if (rect.bottom < clip->bottom)
            mm_region_scan_advance(&cursor.scan, height);
    }
done:
    mm_region_cursor_close(&cursor);
    return ok;
}

void mm_region_set_rect(MMRegion *region, const MMPixelRect *rect)
{
    region->bounds = rect ? *rect : empty;
    bands_release(region->bands);
    region->bands = NULL;
    region->bias = 0;
}

static MMRegionResult set_rectangle(MMRegion *region, const MMPixelRect *rect)
{
    mm_region_set_rect(region, rect);
    return MM_REGION_UPDATED;
}

static MMI32 add_word(MMI32 a, MMI32 b)
{
    return mm_signed_word((MMU32)a + (MMU32)b);
}

void mm_region_offset(MMRegion *region, MMI32 dx, MMI32 dy)
{
    region->bounds.left = add_word(region->bounds.left, dx);
    region->bounds.right = add_word(region->bounds.right, dx);
    region->bounds.top = add_word(region->bounds.top, dy);
    region->bounds.bottom = add_word(region->bounds.bottom, dy);
}

int mm_region_empty(const MMRegion *region)
{
    return rect_empty(&region->bounds);
}

size_t mm_region_rectangle_capacity(const MMRegion *region)
{
    return region->bands ? region->bands->count / 2 : 1;
}

static void builder_init(RegionBuilder *builder, MMRegionBands *bands, int grow, const MMPixelRect *ref)
{
    builder->bands = bands;
    builder->grow = grow;
    builder->reference = *ref;
    builder->bounds.left = ref->right - ref->left;
    builder->bounds.right = 0;
    builder->y = builder->bounds.top = builder->bounds.bottom = ref->top;
    builder->used = 0;
    builder->previous = (size_t)-1;
}

static int start_band(void *context, MMI32 rows, MMI32 max_edges)
{
    RegionBuilder *builder = context;
    MMCollectionStorage *storage = &builder->bands->storage;
    size_t words;
    MMRegionWord *data;
    if (builder->used > MAX_WORDS - 2 || (MMU32)max_edges > MAX_WORDS - builder->used - 2)
        return 0;
    words = builder->used + (size_t)max_edges + 2;
    if (builder->grow)
    {
        words = (words + GROW_WORDS - 1) / GROW_WORDS * GROW_WORDS;
        if (words > MAX_WORDS ||
            mm_storage_ensure(storage, (MMU32)(words * sizeof(*data)), 0, 0) != MM_STORAGE_OK)
            return 0;
    }
    else if (words > builder->bands->count)
        return 0;

    data = (MMRegionWord *)storage->buffers[0].data;
    builder->row = builder->used;
    if (rows > builder->reference.bottom - builder->y)
        rows = builder->reference.bottom - builder->y;
    data[builder->used++] = rows;
    builder->y += rows;
    return 1;
}

static void add_edge(void *context, MMI32 x)
{
    RegionBuilder *builder = context;
    MMRegionWord *data = (MMRegionWord *)builder->bands->storage.buffers[0].data;
    if (builder->bounds.left > x)
        builder->bounds.left = x;
    if (builder->bounds.right < x)
        builder->bounds.right = x;
    data[builder->used++] = x;
}

static void end_band(void *context)
{
    RegionBuilder *builder = context;
    MMRegionWord *data = (MMRegionWord *)builder->bands->storage.buffers[0].data;
    size_t words;
    data[builder->used++] = MM_REGION_END;
    words = builder->used - builder->row;
    if (words > 2)
        builder->bounds.bottom = builder->y;
    if (builder->previous != (size_t)-1 && builder->row - builder->previous == words &&
        !memcmp(data + builder->previous + 1, data + builder->row + 1, (words - 1) * sizeof(*data)))
    {
        data[builder->previous] += data[builder->row];
        builder->used = builder->row;
    }
    else if (!builder->row && words == 2)
    {
        builder->bounds.top = builder->bounds.bottom = builder->y;
        builder->used = 0;
    }
    else
        builder->previous = builder->row;
}

static MMRegionResult publish(MMRegion *region, RegionBuilder *builder)
{
    MMRegionBands *bands = builder->bands;
    if (builder->y > builder->bounds.bottom && builder->previous != (size_t)-1)
        builder->used = builder->previous;
    if (!builder->used)
        builder->bounds = empty;
    else
    {
        builder->bounds.left = add_word(builder->bounds.left, builder->reference.left);
        builder->bounds.right = add_word(builder->bounds.right, builder->reference.left);
    }
    mm_region_set_rect(region, &builder->bounds);
    if (builder->used > 4)
    {
        region->bands = bands;
        region->bias = mm_signed_word((MMU32)builder->reference.left - (MMU32)builder->bounds.left);
        bands->count = builder->used;
        mm_storage_ensure(&bands->storage, (MMU32)(bands->count * sizeof(MMRegionWord)), 0, 1);
    }
    else
        bands_release(bands);
    return MM_REGION_UPDATED;
}

static int representable(const MMPixelRect *r)
{
    return !rect_empty(r) && (MMU32)r->right - (MMU32)r->left <= (MMU32)MM_REGION_END &&
           (MMU32)r->bottom - (MMU32)r->top <= (MMU32)MM_REGION_END;
}

static MMRegionResult combine(MMRegion *target, MMRegionOperation op, const MMRegion *left,
                              const MMRegion *right, const MMPixelRect *bounds, const MMStorageAllocator *allocator)
{
    MMRegionScan a, b;
    RegionBuilder builder;
    MMRegionBands *bands;
    MMRegionBandSink sink = {NULL, start_band, add_edge, end_band};
    if (!representable(bounds) || !(bands = bands_create(allocator)))
        return MM_REGION_FAILED;
    builder_init(&builder, bands, 1, bounds);
    scan_region(&a, left, bounds);
    scan_region(&b, right, bounds);
    sink.context = &builder;
    if (!mm_region_combine(op, &a, &b, bounds->bottom - bounds->top, &sink))
    {
        bands_release(bands);
        return MM_REGION_FAILED;
    }
    return publish(target, &builder);
}

MMRegionResult mm_region_apply(MMRegion *target, MMRegionOperation op, const MMRegion *left,
                               const MMRegion *right, const MMStorageAllocator *allocator)
{
    MMPixelRect bounds;
    if (op == MM_REGION_DIFFERENCE)
    {
        if (!right)
            return MM_REGION_FAILED;
        if (!left)
        {
            bounds = target->bounds;
            mm_pixel_rect_intersect(&bounds, &right->bounds);
            if (rect_empty(&bounds))
                return MM_REGION_UNCHANGED;
            left = target;
        }
        if (mm_region_empty(left))
            return set_rectangle(target, NULL);
        bounds = left->bounds;
    }
    else
    {
        if (!left)
            return MM_REGION_FAILED;
        if (!right)
        {
            if (op == MM_REGION_UNION && mm_region_empty(left))
                return MM_REGION_UNCHANGED;
            right = target;
        }
        bounds = left->bounds;
        if (op == MM_REGION_UNION)
        {
            rect_union(&bounds, &right->bounds);
            if ((!left->bands && rect_equal(&bounds, &left->bounds)) ||
                (!right->bands && rect_equal(&bounds, &right->bounds)))
                return set_rectangle(target, &bounds);
        }
        else if (op == MM_REGION_INTERSECTION)
        {
            mm_pixel_rect_intersect(&bounds, &right->bounds);
            if (rect_empty(&bounds))
                return set_rectangle(target, NULL);
            if (!left->bands && right == target && rect_equal(&bounds, &target->bounds))
                return MM_REGION_UNCHANGED;
            if (!left->bands && !right->bands)
                return set_rectangle(target, &bounds);
        }
        else
            return MM_REGION_FAILED;
    }
    return combine(target, op, left, right, &bounds, allocator);
}

MMRegionResult mm_region_apply_rect(MMRegion *target, MMRegionOperation op, const MMPixelRect *rect,
                                    const MMRegion *other, int subtract_from_rect, const MMStorageAllocator *allocator)
{
    MMRegion box;
    MMPixelRect bounds, intersection;
    box.bounds = *rect;
    box.bands = NULL;
    box.bias = 0;
    if (op == MM_REGION_INTERSECTION)
        return mm_region_apply(target, op, &box, other, allocator);
    if (op == MM_REGION_UNION)
    {
        if (!other && rect_empty(rect))
            return MM_REGION_UNCHANGED;
        if (!other)
            other = target;
        bounds = *rect;
        rect_union(&bounds, &other->bounds);
        if (rect_equal(&bounds, rect))
            return set_rectangle(target, &bounds);
        if (other == target && !target->bands && rect_equal(&bounds, &target->bounds))
            return MM_REGION_UNCHANGED;
        return combine(target, op, &box, other, &bounds, allocator);
    }
    if (op != MM_REGION_DIFFERENCE)
        return MM_REGION_FAILED;

    bounds = other ? other->bounds : target->bounds;
    intersection = bounds;
    mm_pixel_rect_intersect(&intersection, rect);
    if (subtract_from_rect)
    {
        if (!other)
            other = target;
        if (rect_empty(&intersection))
            return set_rectangle(target, rect);
        if (!other->bands && rect_equal(&intersection, rect))
            return set_rectangle(target, NULL);
        return combine(target, op, &box, other, rect, allocator);
    }
    if (!other && rect_empty(&intersection))
        return MM_REGION_UNCHANGED;
    if (rect_empty(&bounds) || rect_equal(&intersection, &bounds))
        return set_rectangle(target, NULL);
    return combine(target, op, other ? other : target, &box, &bounds, allocator);
}

static MMI32 scale_word(MMI32 value, MMI32 numerator, MMI32 denominator)
{
    MMU32 magnitude;
    if (numerator == denominator)
        return value;
    magnitude = value < 0 ? 0 - (MMU32)value : (MMU32)value;
    magnitude = mm_pixel_muldiv(magnitude, (MMU32)numerator, (MMU32)denominator);
    return mm_signed_word(value < 0 ? 0 - magnitude : magnitude);
}

static void scale_edges(RegionBuilder *builder, MMRegionScan *scan, MMI32 numerator, MMI32 denominator)
{
    MMI32 left, right, start = 0, end = 0;
    while (scan->x != MM_REGION_END)
    {
        left = scale_word(scan->x, numerator, denominator);
        right = scale_word(mm_region_scan_fetch(scan), numerator, denominator);
        mm_region_scan_fetch(scan);
        if (left >= right)
            continue;
        if (end > start && left > end)
        {
            add_edge(builder, start);
            add_edge(builder, end);
            start = left;
        }
        else if (start == end)
            start = left;
        end = right;
    }
    if (start < end)
    {
        add_edge(builder, start);
        add_edge(builder, end);
    }
}

MMRegionResult mm_region_scale(MMRegion *region, MMI32 x_num, MMI32 x_den, MMI32 y_num, MMI32 y_den)
{
    MMPixelRect scaled, original = region->bounds;
    RegionBuilder builder;
    MMRegionScan scan;
    MMRegionBands *bands = region->bands;
    MMI32 height, y, scaled_y;
    int shared;
    if (x_den <= 0 || y_den <= 0 || x_num < 0 || y_num < 0)
        return MM_REGION_FAILED;
    scaled.left = scale_word(original.left, x_num, x_den);
    scaled.right = scale_word(original.right, x_num, x_den);
    scaled.top = scale_word(original.top, y_num, y_den);
    scaled.bottom = scale_word(original.bottom, y_num, y_den);
    if (rect_empty(&scaled))
        return set_rectangle(region, NULL);
    if (!bands)
        return set_rectangle(region, &scaled);
    if (!representable(&scaled))
        return MM_REGION_FAILED;

    shared = bands->references > 1;
    if (shared)
    {
        bands = bands_create(&bands->storage.allocator);
        if (!bands)
            return MM_REGION_FAILED;
    }
    else
        ++bands->references;

    builder_init(&builder, bands, shared, &scaled);
    scan_region(&scan, region, &original);
    for (y = original.top; y < original.bottom; y += height)
    {
        height = scan.rows;
        scaled_y = scale_word(y + height, y_num, y_den);
        if (scaled_y > builder.y)
        {
            if (!start_band(&builder, scaled_y - builder.y, mm_region_scan_edge_count(&scan)))
            {
                bands_release(bands);
                return MM_REGION_FAILED;
            }
            scale_edges(&builder, &scan, x_num, x_den);
            end_band(&builder);
        }
        if (builder.y >= scaled.bottom)
            break;
        mm_region_scan_advance(&scan, height);
    }
    return publish(region, &builder);
}
