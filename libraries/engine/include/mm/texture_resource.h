/* Texture map policy converted from bren/tmap.cpp. */
#ifndef MM_ENGINE_TEXTURE_RESOURCE_H
#define MM_ENGINE_TEXTURE_RESOURCE_H
#include "read_stream.h"
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMTextureDescription
    {
        MMI32 row_bytes, width, height, base_x, base_y, origin_x, origin_y;
        unsigned type, flags;
    } MMTextureDescription;
    typedef struct MMTextureResource
    {
        MMTextureDescription description;
        void *pixels, *imported_owner;
    } MMTextureResource;
    typedef struct MMTextureColour
    {
        unsigned char red, green, blue;
    } MMTextureColour;
    typedef struct MMTextureBitmap
    {
        void *pixels, *palette;
        MMI32 width, height;
        int upside_down;
    } MMTextureBitmap;
    enum
    {
        MM_TEXTURE_ORDER,
        MM_TEXTURE_UPSIDE_DOWN
    };
    enum
    {
        MM_TEXTURE_FILE_CREATE,
        MM_TEXTURE_FILE_MEMORY,
        MM_TEXTURE_FILE_WRITE
    };
    typedef struct MMTextureServices
    {
        void *context;
        int host_big_endian;
        MMU32 system, packed_signature, unpacked_signature;
        unsigned indexed_type, linear_flag;
        void *(*create)(void *);
        void (*release)(void *, void *);
        MMTextureResource *(*state)(void *, void *);
        void (*publish)(void *, void *object, const MMTextureResource *);
        void (*describe_import)(void *, void *owner, MMTextureDescription *, void **pixels);
        void (*free_import)(void *, void *owner);
        int (*allocate)(void *, MMI32 bytes, int clear, void **);
        void (*free_memory)(void *, void **);
        void (*invalidate)(void *, void *, size_t);
        void (*diagnostic)(void *, int);
        void *(*file)(void *, void *cache);
        void *(*find)(void *, void *file, MMU32 type, MMU32 number);
        int (*unpack)(void *, void *block);
        MMI32 (*size)(void *, void *block, int packed);
        int (*read)(void *, void *block, void *, MMI32 bytes, MMI32 offset);
        int (*write)(void *, void *block, const void *, MMI32 bytes, MMI32 offset);
        void (*close_block)(void *);
        void *(*add)(void *, void *file, MMU32 type, MMI32 bytes, MMU32 *number);
        int (*read_bitmap)(void *, void *filename, MMTextureBitmap *);
        MMI32 (*palette_count)(void *, void *palette);
        void (*palette_colour)(void *, void *palette, MMI32 index, MMTextureColour *);
        void *(*create_file)(void *, void *filename);
        void *(*temp_block)(void *, MMI32 bytes);
        int (*pack)(void *, void *block);
        int (*write_file)(void *, void *file, const void *, MMI32 bytes, MMI32 offset);
        int (*copy_to_file)(void *, void *block, void *file, MMI32 offset, MMI32 bytes);
        void (*make_temporary)(void *, void *file);
        void (*report)(void *, void *sink, int error);
    } MMTextureServices;
    /* Fresh owners are zeroed. Pixel construction transfers ownership only on
     * success; wrapping transfers the entire renderer allocation only on success.
     * A renderer view may mirror this state, but ownership belongs here. Imported
     * storage is freed through its original owner, never through its pixel pointer.
     * Callbacks are synchronous and must not reenter/mutate the resource. */
    void *mm_texture_new(void *pixels, MMI32 width, MMI32 height, const MMTextureServices *);
    void *mm_texture_wrap(void *owner, const MMTextureServices *);
    void mm_texture_dispose(MMTextureResource *, const MMTextureServices *);
    int mm_texture_header_read(const unsigned char[20], int host_big, MMTextureDescription *,
                                     int *bad_order);
    void mm_texture_header_write(unsigned char[20], int host_big, MMU32 system,
                                       const MMTextureDescription *);
    int mm_texture_payload(const MMTextureDescription *, MMI32 *bytes);
    MMI32 mm_texture_size(const MMTextureResource *);
    /* Read a decoded TMAP extent without borrowing it beyond the call. */
    void *mm_texture_read_stream(const MMReadStream *, const MMTextureServices *);
    void *mm_texture_read(void *file, MMU32 type, MMU32 number, const MMTextureServices *);
    int mm_texture_fetch(void *cache, MMU32 type, MMU32 number, void *estimate_block, void *object_out,
                               void *size_out, const MMTextureServices *);
    int mm_texture_write(const MMTextureResource *, void *block, const MMTextureServices *);
    int mm_texture_write_chunk(const MMTextureResource *, void *file, MMU32 type, MMU32 *number,
                                     const MMTextureServices *);
    /* Palette matching keeps the last equal-distance candidate. Cache allocation is
     * optional. Import preserves the legacy contiguous width*height processing of
     * the bitmap reader's padded buffer; it does not repack rows or flip them. */
    void *mm_texture_bitmap(void *filename, void *palette, const MMTextureServices *);
    int mm_texture_standalone(const MMTextureResource *, void *filename, int compress, void *sink,
                                    const MMTextureServices *);
#ifdef __cplusplus
}
#endif
#endif
