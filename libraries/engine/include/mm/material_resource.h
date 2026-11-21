/* Material resources converted from engine mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MATERIAL_RESOURCE_H
#define MM_ENGINE_MATERIAL_RESOURCE_H
#include "resource_cache.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMaterialResource
    {
        void *handle;
    } MMMaterialResource;
    typedef struct MMMaterialShade
    {
        void *texture;
    } MMMaterialShade;
    typedef struct MMCustomMaterial
    {
        void **materials, **models;
        MMI32 count, set;
    } MMCustomMaterial;
    typedef struct MMMaterialDescription
    {
        MMU32 colour, ambient, diffuse, specular;
        MMI32 power;
        unsigned index_base, index_range, opacity, flags;
    } MMMaterialDescription;
    typedef struct MMMaterialChild
    {
        MMU32 type, number, id;
    } MMMaterialChild;
    enum { MM_MATERIAL_CHUNK = 0x4d54524c };
    typedef struct MMCustomMaterialPart
    {
        MMMaterialChild material, model;
    } MMCustomMaterialPart;
    enum
    {
        MM_MATERIAL,
        MM_CUSTOM_MATERIAL
    };
    enum
    {
        MM_MATERIAL_BAD_FILE,
        MM_MATERIAL_BAD_CUSTOM_SIZE,
        MM_MATERIAL_BAD_CUSTOM_ORDER,
        MM_MATERIAL_BAD_TRANSFORM_ORDER
    };
    typedef struct MMMaterialServices
    {
        void *context;
        int host_big_endian;
        MMU32 system, windows_system, texture_type, transform_type;
        MMI32 material_bytes, custom_bytes;
        unsigned light_flag, gouraud_flag, smooth_flag, map_flag;
        void *(*create)(void *, int kind);
        void (*retain)(void *, void *), (*release)(void *, void *);
        MMMaterialResource *(*material_state)(void *, void *);
        MMCustomMaterial *(*custom_state)(void *, void *);
        void (*invalidate)(void *, void *, size_t);
        void (*diagnostic)(void *, int);
        MMBlockTransform decode;
        MMResourceLoader texture_reader, material_reader, model_reader;
        int (*allocate)(void *, MMI32 bytes, void **);
        void (*free_memory)(void *, void **);
        void *(*allocate_material)(void *);
        void (*identify)(void *, void *handle, void *object);
        void (*describe)(void *, void *handle, MMMaterialDescription *);
        void (*configure)(void *, void *handle, const MMMaterialDescription *);
        void (*add_material)(void *, void *), (*remove_material)(void *, void *), (*free_material)(void *, void *);
        void *(*texture)(void *, void *handle);
        void *(*texture_pixelmap)(void *, void *texture);
        void (*map)(void *, void *handle, void *colour_map, void *shade_map);
        MMI32 (*height)(void *, void *pixelmap);
        void (*transform)(void *, void *handle, const MMI32 matrix[6]);
        int (*add_chunk)(void *, void *file, MMU32 type, const void *, MMI32 bytes, MMU32 *number);
        int (*write_texture)(void *, void *texture, void *file, MMU32 type, MMU32 *number);
        int (*adopt)(void *, void *file, MMU32 type, MMU32 number, const MMMaterialChild *);
        void (*delete_chunk)(void *, void *file, MMU32 type, MMU32 number);
        void *(*read_bitmap)(void *, void *filename, void *palette);
        const void *(*pixelmap_path)(void *, void *filename);
        void *(*read_pixelmap)(void *, const void *path);
        void *(*wrap_pixelmap)(void *, void *pixelmap);
        void (*free_pixelmap)(void *, void *pixelmap);
        void *(*input_material)(void *, const void *input, MMI32 index);
        void *(*material_handle)(void *, void *material);
    } MMMaterialServices;
    /* Zero-initialize fresh owners. Direct read failure keeps partial ownership,
     * except transform failure explicitly releases texture and renderer material.
     * Material factory invalidates before release; custom factory releases first.
     * Input blocks are borrowed and may be unpacked in place. With no block,
     * the reader owns a temporary block obtained from the cache. The same
     * cache supplies the archive graph and fetched dependencies.
     * Callback inputs do not alias owned arrays and callbacks do not mutate owners. */
    void *mm_material_new(MMI32 base, MMI32 range, const MMMaterialServices *);
    int mm_material_fetch(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void *object_out, void *size_out,
                                const MMMaterialShade *, const MMMaterialServices *);
    int mm_material_read(void *object, MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *,
                               const MMMaterialShade *, const MMMaterialServices *);
    void mm_material_dispose(MMMaterialResource *, const MMMaterialServices *);
    void *mm_material_texture(const MMMaterialResource *, const MMMaterialServices *);
    int mm_material_write(const MMMaterialResource *, void *file, MMU32 type, MMU32 *number,
                                const MMMaterialServices *);
    /* Write a supplied appearance and optional slot-zero transform. */
    int mm_material_write_appearance(const MMMaterialResource *, void *file, MMU32 type, MMU32 *number,
                                     const MMMaterialDescription *, const MMI32 mapping[6],
                                     const MMMaterialServices *);
    /* Take an owned texture reference. A null replacement leaves the table installed. */
    int mm_material_shade_set(MMMaterialShade *, void *texture, const MMMaterialServices *);
    void mm_material_shade_dispose(MMMaterialShade *, const MMMaterialServices *);
    void *mm_material_import(void *filename, void *palette, int bitmap, const MMMaterialShade *,
                                   const MMMaterialServices *);
    /* Header decoding fills wire fields; opacity and flags are renderer policy. */
    int mm_material_header_read(const unsigned char[20], int host_big, MMU32 windows_system,
                                      MMMaterialDescription *);
    void mm_material_header_write(unsigned char[20], int host_big, MMU32 system,
                                        const MMMaterialDescription *);
    void *mm_custom_material_new(MMI32 set, MMI32 count, const void *materials,
                                       const MMMaterialServices *);
    int mm_custom_material_fetch(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void *object_out,
                                       void *size_out, const MMMaterialServices *);
    /* Decode the CMTL header's set and optionally report an unrecognized order marker. */
    MMI32 mm_custom_material_read_set(const unsigned char[8], int host_big, int *bad_order);
    /* Resolve the exact MTRL and BMDL links at a costume slot. An absent link
     * gives a zero type. Dangling links and unreadable graph entries fail
     * without publishing the part. Returns an MMChunkResult. */
    int mm_custom_material_part(const MMChunkAccess *, MMU32 costume_position, MMU32 slot,
                                MMCustomMaterialPart *);
    int mm_custom_material_read(void *object, MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *,
                                      const MMMaterialServices *);
    void mm_custom_material_dispose(MMCustomMaterial *, const MMMaterialServices *);
    int mm_custom_material_has_models(const MMCustomMaterial *);
    void *mm_custom_material_model(const MMCustomMaterial *, MMI32 index);
    void *mm_custom_material_handle(const MMCustomMaterial *, MMI32 index,
                                          const MMMaterialServices *);
#ifdef __cplusplus
}
#endif
#endif
