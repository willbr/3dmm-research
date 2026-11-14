/* Material resources converted from engine mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MATERIAL_RESOURCE_H
#define THREE_MM_ENGINE_MATERIAL_RESOURCE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMMaterialResource
    {
        void *handle;
    } ThreeMMMaterialResource;
    typedef struct ThreeMMMaterialShade
    {
        void *texture;
    } ThreeMMMaterialShade;
    typedef struct ThreeMMCustomMaterial
    {
        void **materials, **models;
        ThreeMMI32 count, set;
    } ThreeMMCustomMaterial;
    typedef struct ThreeMMMaterialDescription
    {
        ThreeMMU32 colour, ambient, diffuse, specular;
        ThreeMMI32 power;
        unsigned index_base, index_range, opacity, flags;
    } ThreeMMMaterialDescription;
    typedef struct ThreeMMMaterialChild
    {
        ThreeMMU32 type, number, id;
    } ThreeMMMaterialChild;
    enum
    {
        THREE_MM_MATERIAL,
        THREE_MM_CUSTOM_MATERIAL,
        THREE_MM_MATERIAL_TEXTURE,
        THREE_MM_MATERIAL_MODEL
    };
    enum
    {
        THREE_MM_MATERIAL_BAD_FILE,
        THREE_MM_MATERIAL_BAD_CUSTOM_SIZE,
        THREE_MM_MATERIAL_BAD_CUSTOM_ORDER,
        THREE_MM_MATERIAL_BAD_TRANSFORM_ORDER
    };
    typedef struct ThreeMMMaterialServices
    {
        void *context;
        int host_big_endian;
        ThreeMMU32 system, windows_system, texture_type, transform_type, material_type, model_type;
        ThreeMMI32 material_bytes, custom_bytes;
        unsigned light_flag, gouraud_flag, smooth_flag, map_flag;
        void *(*create)(void *, int kind);
        void (*retain)(void *, void *), (*release)(void *, void *);
        ThreeMMMaterialResource *(*material_state)(void *, void *);
        ThreeMMCustomMaterial *(*custom_state)(void *, void *);
        void (*invalidate)(void *, void *, size_t);
        void (*diagnostic)(void *, int);
        void *(*file)(void *, void *cache);
        void *(*find)(void *, void *file, ThreeMMU32 type, ThreeMMU32 number);
        int (*unpack)(void *, void *block);
        ThreeMMI32 (*size)(void *, void *block);
        int (*read)(void *, void *block, void *, ThreeMMI32 bytes);
        void (*close_block)(void *);
        int (*child)(void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 id, ThreeMMU32 child_type,
                     ThreeMMMaterialChild *);
        int (*child_at)(void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 index,
                        ThreeMMMaterialChild *);
        void *(*fetch)(void *, void *cache, const ThreeMMMaterialChild *, int kind);
        int (*allocate)(void *, ThreeMMI32 bytes, void **);
        void (*free_memory)(void *, void **);
        void *(*allocate_material)(void *);
        void (*identify)(void *, void *handle, void *object);
        void (*describe)(void *, void *handle, ThreeMMMaterialDescription *);
        void (*configure)(void *, void *handle, const ThreeMMMaterialDescription *);
        void (*add_material)(void *, void *), (*remove_material)(void *, void *), (*free_material)(void *, void *);
        void *(*texture)(void *, void *handle);
        void (*map)(void *, void *handle, void *texture, void *raw_pixelmap, void *shade);
        ThreeMMI32 (*height)(void *, void *texture);
        void (*transform)(void *, void *handle, const ThreeMMI32 matrix[6]);
        int (*add_chunk)(void *, void *file, ThreeMMU32 type, const void *, ThreeMMI32 bytes, ThreeMMU32 *number);
        int (*write_texture)(void *, void *texture, void *file, ThreeMMU32 type, ThreeMMU32 *number);
        int (*adopt)(void *, void *file, ThreeMMU32 type, ThreeMMU32 number, const ThreeMMMaterialChild *);
        void (*delete_chunk)(void *, void *file, ThreeMMU32 type, ThreeMMU32 number);
        void *(*read_texture)(void *, void *file, ThreeMMU32 type, ThreeMMU32 number);
        void *(*read_bitmap)(void *, void *filename, void *palette);
        void *(*read_pixelmap)(void *, void *filename);
        void *(*wrap_pixelmap)(void *, void *pixelmap);
        void (*free_pixelmap)(void *, void *pixelmap);
        void *(*input_material)(void *, const void *input, ThreeMMI32 index);
        void *(*material_handle)(void *, void *material);
    } ThreeMMMaterialServices;
    /* Zero-initialize fresh owners. Direct read failure keeps partial ownership,
     * except transform failure explicitly releases texture and renderer material.
     * Material factory invalidates before release; custom factory releases first.
     * Callback inputs do not alias owned arrays and callbacks do not mutate owners. */
    void *three_mm_material_new(ThreeMMI32 base, ThreeMMI32 range, const ThreeMMMaterialServices *);
    int three_mm_material_fetch(void *cache, ThreeMMU32 type, ThreeMMU32 number, void *object_out, void *size_out,
                                const ThreeMMMaterialShade *, const ThreeMMMaterialServices *);
    int three_mm_material_read(void *object, void *cache, ThreeMMU32 type, ThreeMMU32 number,
                               const ThreeMMMaterialShade *, const ThreeMMMaterialServices *);
    void three_mm_material_dispose(ThreeMMMaterialResource *, const ThreeMMMaterialServices *);
    void *three_mm_material_texture(const ThreeMMMaterialResource *, const ThreeMMMaterialServices *);
    int three_mm_material_write(const ThreeMMMaterialResource *, void *file, ThreeMMU32 type, ThreeMMU32 *number,
                                const ThreeMMMaterialServices *);
    int three_mm_material_shade_set(ThreeMMMaterialShade *, void *file, ThreeMMU32 type, ThreeMMU32 number,
                                    const ThreeMMMaterialServices *);
    void three_mm_material_shade_dispose(ThreeMMMaterialShade *, const ThreeMMMaterialServices *);
    void *three_mm_material_import(void *filename, void *palette, int bitmap, const ThreeMMMaterialShade *,
                                   const ThreeMMMaterialServices *);
    /* Header decoding fills wire fields; opacity and flags are renderer policy. */
    int three_mm_material_header_read(const unsigned char[20], int host_big, ThreeMMU32 windows_system,
                                      ThreeMMMaterialDescription *);
    void three_mm_material_header_write(unsigned char[20], int host_big, ThreeMMU32 system,
                                        const ThreeMMMaterialDescription *);
    void *three_mm_custom_material_new(ThreeMMI32 set, ThreeMMI32 count, const void *materials,
                                       const ThreeMMMaterialServices *);
    int three_mm_custom_material_fetch(void *cache, ThreeMMU32 type, ThreeMMU32 number, void *object_out,
                                       void *size_out, const ThreeMMMaterialServices *);
    int three_mm_custom_material_read(void *object, void *cache, ThreeMMU32 type, ThreeMMU32 number,
                                      const ThreeMMMaterialServices *);
    void three_mm_custom_material_dispose(ThreeMMCustomMaterial *, const ThreeMMMaterialServices *);
    int three_mm_custom_material_has_models(const ThreeMMCustomMaterial *);
    void *three_mm_custom_material_model(const ThreeMMCustomMaterial *, ThreeMMI32 index);
    void *three_mm_custom_material_handle(const ThreeMMCustomMaterial *, ThreeMMI32 index,
                                          const ThreeMMMaterialServices *);
#ifdef __cplusplus
}
#endif
#endif
