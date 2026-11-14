/* Texture map policy converted from bren/tmap.cpp. */
#ifndef THREE_MM_ENGINE_TEXTURE_RESOURCE_H
#define THREE_MM_ENGINE_TEXTURE_RESOURCE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMTextureDescription
    {
        ThreeMMI32 row_bytes, width, height, base_x, base_y, origin_x, origin_y;
        unsigned type, flags;
    } ThreeMMTextureDescription;
    typedef struct ThreeMMTextureResource
    {
        ThreeMMTextureDescription description;
        void *pixels, *imported_owner;
    } ThreeMMTextureResource;
    typedef struct ThreeMMTextureColour
    {
        unsigned char red, green, blue;
    } ThreeMMTextureColour;
    typedef struct ThreeMMTextureBitmap
    {
        void *pixels, *palette;
        ThreeMMI32 width, height;
        int upside_down;
    } ThreeMMTextureBitmap;
    enum
    {
        THREE_MM_TEXTURE_ORDER,
        THREE_MM_TEXTURE_UPSIDE_DOWN
    };
    enum
    {
        THREE_MM_TEXTURE_FILE_CREATE,
        THREE_MM_TEXTURE_FILE_MEMORY,
        THREE_MM_TEXTURE_FILE_WRITE
    };
    typedef struct ThreeMMTextureServices
    {
        void *context;
        int host_big_endian;
        ThreeMMU32 system, packed_signature, unpacked_signature;
        unsigned indexed_type, linear_flag;
        void *(*create)(void *);
        void (*release)(void *, void *);
        ThreeMMTextureResource *(*state)(void *, void *);
        void (*publish)(void *, void *object, const ThreeMMTextureResource *);
        void (*describe_import)(void *, void *owner, ThreeMMTextureDescription *, void **pixels);
        void (*free_import)(void *, void *owner);
        int (*allocate)(void *, ThreeMMI32 bytes, int clear, void **);
        void (*free_memory)(void *, void **);
        void (*invalidate)(void *, void *, size_t);
        void (*diagnostic)(void *, int);
        void *(*file)(void *, void *cache);
        void *(*find)(void *, void *file, ThreeMMU32 type, ThreeMMU32 number);
        int (*unpack)(void *, void *block);
        ThreeMMI32 (*size)(void *, void *block, int packed);
        int (*read)(void *, void *block, void *, ThreeMMI32 bytes, ThreeMMI32 offset);
        int (*write)(void *, void *block, const void *, ThreeMMI32 bytes, ThreeMMI32 offset);
        void (*close_block)(void *);
        void *(*add)(void *, void *file, ThreeMMU32 type, ThreeMMI32 bytes, ThreeMMU32 *number);
        int (*read_bitmap)(void *, void *filename, ThreeMMTextureBitmap *);
        ThreeMMI32 (*palette_count)(void *, void *palette);
        void (*palette_colour)(void *, void *palette, ThreeMMI32 index, ThreeMMTextureColour *);
        void *(*create_file)(void *, void *filename);
        void *(*temp_block)(void *, ThreeMMI32 bytes);
        int (*pack)(void *, void *block);
        int (*write_file)(void *, void *file, const void *, ThreeMMI32 bytes, ThreeMMI32 offset);
        int (*copy_to_file)(void *, void *block, void *file, ThreeMMI32 offset, ThreeMMI32 bytes);
        void (*make_temporary)(void *, void *file);
        void (*report)(void *, void *sink, int error);
    } ThreeMMTextureServices;
    /* Fresh owners are zeroed. Pixel construction transfers ownership only on
     * success; wrapping transfers the entire renderer allocation only on success.
     * A renderer view may mirror this state, but ownership belongs here. Imported
     * storage is freed through its original owner, never through its pixel pointer.
     * Callbacks are synchronous and must not reenter/mutate the resource. */
    void *three_mm_texture_new(void *pixels, ThreeMMI32 width, ThreeMMI32 height, const ThreeMMTextureServices *);
    void *three_mm_texture_wrap(void *owner, const ThreeMMTextureServices *);
    void three_mm_texture_dispose(ThreeMMTextureResource *, const ThreeMMTextureServices *);
    int three_mm_texture_header_read(const unsigned char[20], int host_big, ThreeMMTextureDescription *,
                                     int *bad_order);
    void three_mm_texture_header_write(unsigned char[20], int host_big, ThreeMMU32 system,
                                       const ThreeMMTextureDescription *);
    int three_mm_texture_payload(const ThreeMMTextureDescription *, ThreeMMI32 *bytes);
    ThreeMMI32 three_mm_texture_size(const ThreeMMTextureResource *);
    void *three_mm_texture_read(void *file, ThreeMMU32 type, ThreeMMU32 number, const ThreeMMTextureServices *);
    int three_mm_texture_fetch(void *cache, ThreeMMU32 type, ThreeMMU32 number, void *estimate_block, void *object_out,
                               void *size_out, const ThreeMMTextureServices *);
    int three_mm_texture_write(const ThreeMMTextureResource *, void *block, const ThreeMMTextureServices *);
    int three_mm_texture_write_chunk(const ThreeMMTextureResource *, void *file, ThreeMMU32 type, ThreeMMU32 *number,
                                     const ThreeMMTextureServices *);
    /* Palette matching keeps the last equal-distance candidate. Cache allocation is
     * optional. Import preserves the legacy contiguous width*height processing of
     * the bitmap reader's padded buffer; it does not repack rows or flip them. */
    void *three_mm_texture_bitmap(void *filename, void *palette, const ThreeMMTextureServices *);
    int three_mm_texture_standalone(const ThreeMMTextureResource *, void *filename, int compress, void *sink,
                                    const ThreeMMTextureServices *);
#ifdef __cplusplus
}
#endif
#endif
