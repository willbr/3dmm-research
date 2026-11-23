/* Material resources converted from engine mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MATERIAL_RESOURCE_H
#define MM_ENGINE_MATERIAL_RESOURCE_H
#include "chunk_storage.h"
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
    typedef struct MMMaterialAppearance
    {
        MMU32 colour;
        unsigned index_base, index_range;
        float ambient, diffuse, specular, shininess, mapping[6];
        int textured;
    } MMMaterialAppearance;
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
        void (*retain)(void *, void *), (*release)(void *, void *); /* Optional dependency adapters. */
        void (*invalidate)(void *, void *, size_t);
        void (*diagnostic)(void *, int);
        MMBlockTransform decode;
        MMResourceBlockReader texture_reader, material_reader, model_reader;
        const MMResourceObjectServices *texture_objects, *material_objects, *model_objects;
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
        int (*write_texture)(void *, void *texture, const MMChunkEdit *, const MMBlockFileServices *,
                             MMU32 type, MMU32 *number);
        void *(*read_bitmap)(void *, void *filename, void *palette);
        void *(*read_pixelmap)(void *, const void *path);
        void *(*wrap_pixelmap)(void *, void *pixelmap);
        void (*free_pixelmap)(void *, void *pixelmap);
        void *(*input_material)(void *, const void *input, MMI32 index);
        void *(*material_handle)(void *, void *material);
        int (*read_mapping)(void *, void *handle, MMI32 matrix[6]);
    } MMMaterialServices;
    typedef struct MMMaterialOwner MMMaterialOwner;
    typedef struct MMMaterialOwnerHost
    {
        const MMMaterialServices *services;
        const MMMaterialShade *shade;
        const MMStorageAllocator *objects;
        void (*mark_memory)(void *);
        void (*mark_cache)(MMResourceCache *);
        void (*mark_texture)(void *);
        void (*mark_model)(void *);
    } MMMaterialOwnerHost;
    /* The host and its providers outlive their owners. A fresh owner starts
     * with one reference and an empty payload. The same lifetime operations
     * handle ordinary materials and costumes, including mixed body snapshots. */
    MMMaterialOwner *mm_material_owner_create(const MMMaterialOwnerHost *, int kind);
    const MMMaterialOwnerHost *mm_material_owner_host(const MMMaterialOwner *);
    const MMResourceObjectServices *mm_material_owner_services(void);
    const MMResourceObject *mm_material_owner_binding(const MMMaterialOwner *);
    int mm_material_owner_kind(const MMMaterialOwner *);
    MMI32 mm_material_owner_references(const MMMaterialOwner *);
    MMMaterialResource *mm_material_owner_resource(MMMaterialOwner *);
    MMCustomMaterial *mm_material_owner_custom(MMMaterialOwner *);
    void mm_material_owner_retain(MMMaterialOwner *);
    void mm_material_owner_release(MMMaterialOwner *);
    void *mm_material_owner_texture(MMMaterialOwner *);
    int mm_material_owner_write(MMMaterialOwner *, const MMArchiveWriter *, MMU32 type, MMU32 *number);
    void mm_material_owner_read_view(MMMaterialOwner *, MMMaterialAppearance *);
    int mm_material_owner_write_view(MMMaterialOwner *, const MMArchiveWriter *, MMU32 type, MMU32 *number,
                                     const MMMaterialAppearance *);
    /* Direct read failure keeps partial ownership, except on transform failure
     * which releases both texture and renderer material. Input blocks are
     * borrowed and can be unpacked in place. Without a block, the reader binds
     * a temporary block from the same cache that supplies its dependencies.
     * Material factory invalidates output before release and costume releases first. */
    MMMaterialOwner *mm_material_new(MMI32 base, MMI32 range, const MMMaterialOwnerHost *);
    int mm_material_fetch(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **object, MMI32 *bytes,
                          const MMMaterialOwnerHost *);
    int mm_material_read(MMMaterialOwner *, MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *);
    void *mm_material_texture(const MMMaterialResource *, const MMMaterialServices *);
    int mm_material_write(const MMMaterialResource *, const MMChunkEdit *, const MMBlockFileServices *,
                           MMU32 type, MMU32 *number, const MMMaterialServices *);
    /* Write a supplied appearance and optional slot-zero transform. */
    int mm_material_write_appearance(const MMMaterialResource *, const MMChunkEdit *, const MMBlockFileServices *,
                                     MMU32 type, MMU32 *number, const MMMaterialDescription *, const MMI32 mapping[6],
                                     const MMMaterialServices *);
    /* Texture presence comes from the source material. Validation and equality
     * cover the editable colour, lighting, palette and mapping values. */
    int mm_material_appearance_valid(const MMMaterialAppearance *);
    int mm_material_appearance_equal(const MMMaterialAppearance *, const MMMaterialAppearance *);
    void mm_material_read_view(const MMMaterialResource *, MMMaterialAppearance *, const MMMaterialServices *);
    int mm_material_write_view(const MMMaterialResource *, const MMArchiveWriter *, MMU32 type, MMU32 *number,
                               const MMMaterialAppearance *, const MMMaterialServices *);
    /* Take an owned texture reference. A null replacement leaves the table installed. */
    int mm_material_shade_set(MMMaterialShade *, void *texture, const MMMaterialServices *);
    void mm_material_shade_dispose(MMMaterialShade *, const MMMaterialServices *);
    /* Source is a host filename for bitmap imports and a renderer path otherwise. */
    MMMaterialOwner *mm_material_import(void *source, void *palette, int bitmap, const MMMaterialOwnerHost *);
    /* Header decoding fills wire fields; opacity and flags are renderer policy. */
    int mm_material_header_read(const unsigned char[20], int host_big, MMU32 windows_system,
                                      MMMaterialDescription *);
    void mm_material_header_write(unsigned char[20], int host_big, MMU32 system,
                                        const MMMaterialDescription *);
    /* Author a solid material using the archive writer's wire format. */
    int mm_material_archive_add(const MMArchiveWriter *, const MMMaterialDescription *, MMU32 *number);
    MMMaterialOwner *mm_custom_material_new(MMI32 set, MMI32 count, const void *materials,
                                             const MMMaterialOwnerHost *);
    int mm_custom_material_fetch(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **object,
                                 MMI32 *bytes, const MMMaterialOwnerHost *);
    /* Decode the CMTL header's set and optionally report an unrecognized order marker. */
    MMI32 mm_custom_material_read_set(const unsigned char[8], int host_big, int *bad_order);
    /* Resolve the exact MTRL and BMDL links at a costume slot. An absent link
     * gives a zero type. Dangling links and unreadable graph entries fail
     * without publishing the part. Returns an MMChunkResult. */
    int mm_custom_material_part(const MMChunkAccess *, MMU32 costume_position, MMU32 slot,
                                MMCustomMaterialPart *);
    int mm_custom_material_read(MMMaterialOwner *, MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *);
    void mm_custom_material_dispose(MMCustomMaterial *, const MMMaterialServices *);
    int mm_custom_material_has_models(const MMCustomMaterial *);
    void *mm_custom_material_model(const MMCustomMaterial *, MMI32 index);
    void *mm_custom_material_handle(const MMCustomMaterial *, MMI32 index,
                                          const MMMaterialServices *);
#ifdef __cplusplus
}
#endif
#endif
