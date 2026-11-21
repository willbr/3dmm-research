/* Texture map policy converted from bren/tmap.cpp. */
#ifndef MM_ENGINE_TEXTURE_RESOURCE_H
#define MM_ENGINE_TEXTURE_RESOURCE_H
#include "chunk_storage.h"
#include "resource_cache.h"
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
        MM_TEXTURE_UPSIDE_DOWN,
        MM_TEXTURE_PACK_SHRINK
    };
    enum
    {
        MM_TEXTURE_FILE_CREATE,
        MM_TEXTURE_FILE_MEMORY,
        MM_TEXTURE_FILE_WRITE
    };
    typedef struct MMTextureOwner MMTextureOwner;
    typedef struct MMTextureServices
    {
        void *context;
        int host_big_endian;
        MMU32 system, packed_signature, unpacked_signature;
        unsigned indexed_type, linear_flag;
        void (*release_aux)(void *, void *); /* File and palette references. */
        void (*publish)(void *, MMTextureOwner *, const MMTextureResource *);
        void (*sync)(void *, MMTextureOwner *, MMTextureResource *);
        void (*describe_import)(void *, void *owner, MMTextureDescription *, void **pixels);
        void (*free_import)(void *, void *owner);
        int (*allocate)(void *, MMI32 bytes, int clear, void **);
        void (*free_memory)(void *, void **);
        void (*invalidate)(void *, void *, size_t);
        void (*diagnostic)(void *, int);
        const MMChunkPayloadServices *payloads;
        const MMStorageAllocator *copy_allocator;
        MMBlockTransform encode;
        int (*read_bitmap)(void *, void *filename, MMTextureBitmap *);
        MMI32 (*palette_count)(void *, void *palette);
        void (*palette_colour)(void *, void *palette, MMI32 index, MMTextureColour *);
        void *(*create_file)(void *, void *filename);
        void (*make_temporary)(void *, void *file);
        void (*report)(void *, void *sink, int error);
    } MMTextureServices;
    typedef struct MMTextureOwnerHost
    {
        const MMTextureServices *services;
        const MMStorageAllocator *objects; /* NULL selects the C heap. */
        MMU32 object_bytes;
        void (*mark_memory)(void *);
    } MMTextureOwnerHost;
    /* A host may embed this as the first member of a larger object holding its
     * renderer view. The host and its providers outlive every texture owner. */
    struct MMTextureOwner
    {
        MMResourceObject binding;
        MMReference references;
        MMTextureResource texture;
        const MMTextureOwnerHost *host;
    };
    /* Pixel construction and wrapping transfer the input only on success.
     * Import release uses the original renderer allocation. Sync reads the
     * current renderer view before persistence or cleanup. Inline callbacks
     * may read the owner but must not reenter ownership operations. */
    MMTextureOwner *mm_texture_new(void *pixels, MMI32 width, MMI32 height, const MMTextureOwnerHost *);
    MMTextureOwner *mm_texture_wrap(void *imported, const MMTextureOwnerHost *);
    const MMTextureResource *mm_texture_owner_resource(MMTextureOwner *);
    const MMResourceObjectServices *mm_texture_owner_services(void);
    void mm_texture_owner_release(MMTextureOwner *);
    int mm_texture_header_read(const unsigned char[20], int host_big, MMTextureDescription *,
                                     int *bad_order);
    void mm_texture_header_write(unsigned char[20], int host_big, MMU32 system,
                                       const MMTextureDescription *);
    int mm_texture_payload(const MMTextureDescription *, MMI32 *bytes);
    MMI32 mm_texture_size(const MMTextureResource *);
    /* Read a decoded TMAP extent without borrowing it beyond the call. */
    MMTextureOwner *mm_texture_read_stream(const MMReadStream *, const MMTextureOwnerHost *);
    MMTextureOwner *mm_texture_read(void *file, MMU32 type, MMU32 number, const MMTextureOwnerHost *);
    /* Fetch from the caller's owning cache block and unpack it in place.
     * Estimates report the stored length without decoding. */
    int mm_texture_fetch(MMOwnedBlock *, void **object, MMI32 *bytes, const MMTextureOwnerHost *);
    int mm_texture_write(const MMTextureResource *, MMOwnedBlock *, const MMTextureServices *);
    /* A write failure after creation leaves the published chunk for the caller. */
    int mm_texture_write_chunk(const MMTextureResource *, const MMChunkEdit *, MMU32 type, MMU32 *number,
                               const MMTextureServices *);
    /* Palette matching keeps the last equal-distance candidate. Cache allocation is
     * optional. Import preserves the legacy contiguous width*height processing of
     * the bitmap reader's padded buffer; it does not repack rows or flip them. */
    MMTextureOwner *mm_texture_bitmap(void *filename, void *palette, const MMTextureOwnerHost *);
    int mm_texture_standalone(const MMTextureResource *, void *filename, int compress, void *sink,
                                    const MMTextureServices *);
#ifdef __cplusplus
}
#endif
#endif
