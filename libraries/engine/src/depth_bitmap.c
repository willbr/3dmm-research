/* Depth bitmap policy converted from bren/zbmp.cpp. */
#include "mm/depth_bitmap.h"
#include <stdlib.h>
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
static MMU32 word(const unsigned char *b, int big)
{
    return big ? ((MMU32)b[0] << 8) | b[1] : ((MMU32)b[1] << 8) | b[0];
}
static MMI32 signed_word(MMU32 n)
{
    return n < 32768U ? (MMI32)n : (MMI32)n - 65536L;
}
static void put(unsigned char *b, MMU32 n, int big)
{
    b[big ? 1 : 0] = (unsigned char)n;
    b[big ? 0 : 1] = (unsigned char)(n >> 8);
}
static int layout(MMI32 w, MMI32 h, MMI32 *row, MMI32 *bytes)
{
    if (w < 0 || h < 0 || w > MAXIMUM / 2)
        return 0;
    *row = w * 2;
    if (*row && h > (MAXIMUM - 12) / *row)
        return 0;
    *bytes = *row * h;
    return 1;
}
int mm_depth_header(const unsigned char b[12], int big, MMDepthBitmap *m)
{
    MMI32 w = signed_word(word(b + 8, big)), h = signed_word(word(b + 10, big));
    if (!layout(w, h, &m->row_bytes, &m->bytes))
        return 0;
    m->bounds.left = signed_word(word(b + 4, big));
    m->bounds.top = signed_word(word(b + 6, big));
    m->bounds.right = m->bounds.left + w;
    m->bounds.bottom = m->bounds.top + h;
    return 1;
}
struct MMDepthOwner
{
    MMResourceObject binding;
    MMReference references;
    const MMDepthOwnerHost *host;
    MMDepthBitmap bitmap;
};

static void *allocate(const MMStorageAllocator *allocator, MMU32 bytes)
{
    if (allocator)
        return allocator->resize(allocator->context, NULL, 0, bytes, MM_STORAGE_NORMAL);
    return malloc(bytes ? bytes : 1);
}

static void release(const MMStorageAllocator *allocator, void *memory, MMU32 bytes)
{
    if (memory)
    {
        if (allocator)
            allocator->release(allocator->context, memory, bytes);
        else
            free(memory);
    }
}

static MMDepthOwner *create(const MMDepthOwnerHost *host)
{
    MMDepthOwner *owner = allocate(host ? host->objects : NULL, sizeof(*owner));
    if (owner)
    {
        memset(owner, 0, sizeof(*owner));
        mm_reference_init(&owner->references);
        owner->host = host;
    }
    return owner;
}

const MMDepthBitmap *mm_depth_owner_bitmap(const MMDepthOwner *owner) { return &owner->bitmap; }
const MMResourceObject *mm_depth_owner_binding(const MMDepthOwner *owner) { return &owner->binding; }
static MMResourceObject *binding(void *object) { return &((MMDepthOwner *)object)->binding; }
static MMReference *references(void *object) { return &((MMDepthOwner *)object)->references; }

static void destroy(void *object)
{
    MMDepthOwner *owner = object;
    const MMDepthOwnerHost *host = owner->host;
    release(host ? host->pixels : NULL, owner->bitmap.pixels, owner->bitmap.bytes);
    mm_resource_object_dispose(owner, mm_depth_owner_services());
    release(host ? host->objects : NULL, owner, sizeof(*owner));
}

static void mark(void *object)
{
    MMDepthOwner *owner = object;
    if (owner->host && owner->host->mark)
        owner->host->mark(owner);
}

static void issue(void *object, int kind)
{
    MMDepthOwner *owner = object;
    if (owner->host && owner->host->issue)
        owner->host->issue(owner, kind);
}

const MMResourceObjectServices *mm_depth_owner_services(void)
{
    static const MMResourceObjectServices services = { binding, references, destroy, issue, mark };
    return &services;
}

void mm_depth_owner_release(MMDepthOwner *owner)
{
    if (owner)
        mm_resource_object_release(owner, mm_depth_owner_services());
}

void mm_depth_owner_detach(MMDepthOwner *owner)
{
    mm_resource_object_detach(owner, mm_depth_owner_services());
}

void mm_depth_owner_priority(MMDepthOwner *owner, MMI32 priority)
{
    mm_resource_object_priority(owner, priority, mm_depth_owner_services());
}

MMDepthOwner *mm_depth_owner_new(MMI32 w, MMI32 h, const void *initial, const MMDepthOwnerHost *host)
{
    MMDepthOwner *owner;
    MMDepthBitmap *m;
    MMI32 row, n;
    if (w <= 0 || h <= 0 || !layout(w, h, &row, &n))
        return NULL;
    owner = create(host);
    if (!owner)
        return NULL;
    m = &owner->bitmap;
    m->bounds.right = w;
    m->bounds.bottom = h;
    m->row_bytes = row;
    m->bytes = n;
    m->pixels = allocate(host ? host->pixels : NULL, n);
    if (!m->pixels)
    {
        mm_depth_owner_release(owner);
        return NULL;
    }
    if (initial)
        memcpy(m->pixels, initial, (size_t)n);
    else
        memset(m->pixels, 255, (size_t)n);
    return owner;
}

int mm_depth_native_big_endian(void)
{
    const unsigned short one = 1;
    return *(const unsigned char *)&one == 0;
}

MMDepthOwner *mm_depth_owner_read_stream(const MMReadStream *stream, int big, const MMDepthOwnerHost *host)
{
    MMDepthOwner *owner = create(host);
    MMDepthBitmap *m;
    unsigned char header[12];
    if (!owner)
        return NULL;
    m = &owner->bitmap;
    if (mm_read_exact(stream, 0, header, sizeof(header)) && mm_depth_header(header, big, m) &&
        stream->length == sizeof(header) + (MMU32)m->bytes)
    {
        m->pixels = allocate(host ? host->pixels : NULL, m->bytes);
        if (m->pixels && mm_read_exact(stream, sizeof(header), m->pixels, (size_t)m->bytes))
            return owner;
    }
    mm_depth_owner_release(owner);
    return NULL;
}

MMDepthOwner *mm_depth_owner_read_block(MMOwnedBlock *block, int big, const MMDepthOwnerHost *host)
{
    MMDepthOwner *owner;
    MMBlockIO io;
    MMReadStream stream;
    if (mm_owned_block_code(block, 0, host ? host->context : NULL, host ? host->decode : NULL, NULL) != MM_DATA_OK ||
        mm_owned_block_io(block, &io) != MM_DATA_OK || mm_block_io_stream(&io, &stream) != MM_DATA_OK)
        return NULL;
    owner = mm_depth_owner_read_stream(&stream, big, host);
    if (!owner && host && host->warning)
        host->warning(host->context);
    return owner;
}

int mm_depth_owner_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, MMI32 overhead, int big,
                         const MMDepthOwnerHost *host)
{
    MMDepthOwner *owner;
    if (!out)
    {
        *bytes = (MMI32)block->view.length;
        return 1;
    }
    owner = mm_depth_owner_read_block(block, big, host);
    if (!owner)
        return 0;
    if (overhead > MAXIMUM - owner->bitmap.bytes)
    {
        mm_depth_owner_release(owner);
        return 0;
    }
    *out = owner;
    *bytes = overhead + owner->bitmap.bytes;
    return 1;
}

int mm_depth_write(const MMDepthBitmap *m, void *file, MMU32 type, MMU32 *number,
                         const MMDepthWriteServices *s)
{
    unsigned char header[12];
    MMStorageWriter writer;
    MMU32 size;
    if (m->bytes < 0 || m->bytes > MAXIMUM - 12)
        return 0;
    put(header, 1, s->host_big_endian);
    put(header + 2, s->system, s->host_big_endian);
    put(header + 4, (MMU32)m->bounds.left, s->host_big_endian);
    put(header + 6, (MMU32)m->bounds.top, s->host_big_endian);
    put(header + 8, (MMU32)m->bounds.right - (MMU32)m->bounds.left, s->host_big_endian);
    put(header + 10, (MMU32)m->bounds.bottom - (MMU32)m->bounds.top, s->host_big_endian);
    size = (MMU32)m->bytes + sizeof(header);
    return s->add(s->context, file, type, size, number, &writer) && writer.length == size && writer.write_at &&
           writer.write_at(writer.context, 0, header, sizeof(header)) &&
           writer.write_at(writer.context, sizeof(header), m->pixels, (size_t)m->bytes);
}
static int add(MMI32 a, MMI32 b, MMI32 *out)
{
    if ((b > 0 && a > MAXIMUM - b) || (b < 0 && a < MINIMUM - b))
        return 0;
    *out = a + b;
    return 1;
}
static int empty_rect(const MMDepthRect *rect)
{
    return rect->left >= rect->right || rect->top >= rect->bottom;
}
int mm_depth_draw(const MMDepthBitmap *m, void *pixels, MMI32 row, MMI32 height, MMI32 x,
                        MMI32 y, const MMDepthRect *clip, const MMRegion *region, int half)
{
    MMDepthRect bounds = m->bounds;
    MMRegionCursor cursor = {0};
    MMRegion rectangle = {0};
    MMI32 step = half ? 2 : 1, yp, left, right, sy, bytes, n, src, dst;
    int ok = 0;
    if (!clip && !region)
        return 1;
    if (row < 0 || height < 0 || (row && height / step > MAXIMUM / row) || m->row_bytes < 0 || m->bytes < 0)
        return 0;
    bytes = row * (height / step);
    if (half)
    {
        if (!add(bounds.bottom, bounds.bottom, &bounds.bottom))
            return 0;
    }
    if (!add(bounds.left, x, &bounds.left) || !add(bounds.right, x, &bounds.right) ||
        !add(bounds.top, y, &bounds.top) || !add(bounds.bottom, y, &bounds.bottom))
        return 0;
    if (region)
        mm_pixel_rect_intersect(&bounds, &region->bounds);
    if (clip)
        mm_pixel_rect_intersect(&bounds, clip);
    if (empty_rect(&bounds))
        return 1;
    if (!region)
    {
        rectangle.bounds = bounds;
        region = &rectangle;
    }
    mm_region_cursor_open(&cursor, region, &bounds);
    for (yp = bounds.top; yp < bounds.bottom;)
    {
        while (mm_region_scan_pair(&cursor.scan, &left, &right))
        {
            if (!add(left, bounds.left, &left) || !add(right, bounds.left, &right) || left < bounds.left ||
                right > bounds.right || left < 0 || right < left || right > MAXIMUM / 2 || yp < 0)
                goto done;
            sy = yp / step;
            n = (right - left) * 2;
            if (left > m->row_bytes / 2 || right > m->row_bytes / 2 || right > row / 2 ||
                (m->row_bytes && sy > m->bytes / m->row_bytes) || (row && sy > bytes / row))
                goto done;
            src = sy * m->row_bytes;
            dst = sy * row;
            if (left * 2 > m->bytes - src || n > m->bytes - src - left * 2 || left * 2 > bytes - dst ||
                n > bytes - dst - left * 2 || (n && (!m->pixels || !pixels)))
                goto done;
            if (n)
                memmove((unsigned char *)pixels + dst + left * 2, (const unsigned char *)m->pixels + src + left * 2,
                        (size_t)n);
        }
        if (yp > MAXIMUM - step || yp + step >= bounds.bottom)
            break;
        mm_region_scan_advance(&cursor.scan, step);
        yp += step;
    }
    ok = 1;
done:
    mm_region_cursor_close(&cursor);
    return ok;
}
