/* Model resource policy converted from engine modl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MODEL_RESOURCE_H
#define THREE_MM_ENGINE_MODEL_RESOURCE_H
#include "body_geometry.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMModelResource
    {
        void *handle;
    } ThreeMMModelResource;
    typedef struct ThreeMMModelDescription
    {
        ThreeMMI32 vertices, faces, radius;
        ThreeMMBodyBounds bounds;
        ThreeMMI32 pivot[3];
    } ThreeMMModelDescription;
    typedef struct ThreeMMModelFace
    {
        ThreeMMU32 vertices[3], edges[3], smoothing, flags, pad;
        ThreeMMI32 normal[3], distance;
    } ThreeMMModelFace;
    enum
    {
        THREE_MM_MODEL_VERTICES,
        THREE_MM_MODEL_FACES,
        THREE_MM_MODEL_VERTEX_GROUP,
        THREE_MM_MODEL_FACE_GROUP
    };
    enum
    {
        THREE_MM_MODEL_BAD_ORDER,
        THREE_MM_MODEL_BAD_COUNT,
        THREE_MM_MODEL_MULTIPLE_LIGHTS
    };
    typedef struct ThreeMMModelServices
    {
        void *context;
        int host_big_endian;
        ThreeMMU32 system;
        ThreeMMI32 face_bytes, vertex_group_bytes, face_group_bytes;
        unsigned prepared_flag, keep_original_flag, prepare_all;
        ThreeMMModelResource *(*state)(void *, void *object);
        void *(*create)(void *);
        void (*release)(void *, void *object);
        int (*unpack)(void *, void *block);
        ThreeMMI32 (*size)(void *, void *block, int packed);
        int (*read)(void *, void *block, void *, ThreeMMI32 bytes, ThreeMMI32 offset);
        int (*allocate)(void *, ThreeMMI32 bytes, int clear, void **out);
        void (*free_memory)(void *, void **);
        void (*invalidate)(void *, void *, size_t);
        void (*diagnostic)(void *, int);
        void (*save_failed)(void *);
        void *(*allocate_model)(void *, ThreeMMI32 vertices, ThreeMMI32 faces);
        void (*identify)(void *, void *handle, void *owner);
        void (*copy_input)(void *, void *handle, const void *vertices, const void *faces, ThreeMMI32, ThreeMMI32);
        void (*add)(void *, void *handle);
        void (*remove)(void *, void *handle);
        void (*free_model)(void *, void *handle);
        int (*component)(void *, void *handle, int kind, ThreeMMI32 bytes);
        void *(*vertex_data)(void *, void *handle, int prepared);
        void (*set_face)(void *, void *handle, int prepared, ThreeMMI32, const ThreeMMModelFace *);
        void (*get_face)(void *, void *handle, ThreeMMI32, ThreeMMModelFace *);
        void (*publish_prepared)(void *, void *handle, const ThreeMMModelDescription *, unsigned flag,
                                 ThreeMMI32 vertex_groups, ThreeMMI32 face_groups);
        void (*bind_group)(void *, void *handle, int face_group);
        void (*describe)(void *, void *handle, ThreeMMModelDescription *);
        void (*geometry)(void *, void *handle, const ThreeMMModelDescription *);
        int (*put)(void *, void *file, ThreeMMU32 type, ThreeMMU32 number, const void *, ThreeMMI32);
        void (*position)(void *, void *handle, ThreeMMI32, ThreeMMI32 out[3]);
        void (*set_position)(void *, void *handle, ThreeMMI32, const ThreeMMI32 in[3]);
        const void *(*path)(void *, void *filename);
        void *(*load_dat)(void *, const void *path);
        void (*or_flags)(void *, void *handle, unsigned flags);
        void (*prepare)(void *, void *handle, unsigned flags);
    } ThreeMMModelServices;
    /* Handles are renderer-owned allocations with lifetime controlled here. State
     * starts empty; do not reinitialize a nonempty owner. Direct read failure keeps
     * partial ownership; dispose releases it. Factory failure releases before
     * invalidating outputs, retaining native ordering and 32-bit cache estimates.
     * Read bounds are checked before native I/O. Negative signed wire counts fail
     * cleanly instead of invoking the original allocator/read assertions.
     * Callbacks are synchronous and must not reenter/mutate unrelated state. */
    void three_mm_model_init(ThreeMMModelResource *);
    void three_mm_model_dispose(ThreeMMModelResource *, const ThreeMMModelServices *);
    void *three_mm_model_new(ThreeMMI32, const void *, ThreeMMI32, const void *, const ThreeMMModelServices *);
    int three_mm_model_fetch(void *block, void *object_out, void *size_out, const ThreeMMModelServices *);
    int three_mm_model_read(void *object, void *block, const ThreeMMModelServices *);
    int three_mm_model_write(const ThreeMMModelResource *, void *file, ThreeMMU32 type, ThreeMMU32 number,
                             const ThreeMMModelServices *);
    void *three_mm_model_dat(void *filename, const ThreeMMModelServices *);
    ThreeMMI32 three_mm_model_extent(const ThreeMMModelResource *, unsigned axis, const ThreeMMModelServices *);
    void three_mm_model_adjust_glyph(const ThreeMMModelResource *, const ThreeMMModelServices *);
    /* Wire faces are 32 bytes, with a discarded 32-bit material slot and zeroed
     * implicit padding on output. Vertices are opaque native-order 32-byte records.
     * The old reader normalized its header before testing bo again: foreign vertex
     * and face arrays therefore remain unswapped in both preparation branches. */
    int three_mm_model_header(const unsigned char[48], int host_big, ThreeMMModelDescription *, int *bad_order);
    void three_mm_model_face_read(const unsigned char[32], int big, ThreeMMModelFace *);
    void three_mm_model_face_write(unsigned char[32], int big, const ThreeMMModelFace *);

    enum
    {
        THREE_MM_MODEL_LIGHT_WORLD,
        THREE_MM_MODEL_LIGHT_CAMERA,
        THREE_MM_MODEL_LIGHT_ACTOR,
        THREE_MM_MODEL_LIGHT_COLOUR,
        THREE_MM_MODEL_LIGHT_DEPTH,
        THREE_MM_MODEL_LIGHT_MATERIAL
    };
    typedef struct ThreeMMModelLightingSpec
    {
        ThreeMMU32 colour;
        ThreeMMI32 width, height, fov, hither, yon, aspect, camera_width, camera_height;
        ThreeMMI32 attenuation, ambient, diffuse, specular, power, index_base, index_range;
        int material_lit, material_gouraud;
    } ThreeMMModelLightingSpec;
    typedef struct ThreeMMModelLightingServices
    {
        void *context;
        void *(*create)(void *, int kind, const ThreeMMModelLightingSpec *);
        void (*attach)(void *, void *world, void *actor);
        void (*enable)(void *, void *light);
        void (*disable)(void *, void *light);
        void (*add_material)(void *, void *material);
        void (*remove_material)(void *, void *material);
        void (*begin)(void *, void *world, void *camera, void *colour, void *depth);
        void (*light)(void *, void *model, void *material, void *world);
        void (*end)(void *);
        void (*free)(void *, int kind, void *);
        void (*multiple_lights)(void *);
    } ThreeMMModelLightingServices;
    /* Retains the native default camera/light/material and intentionally ignores
     * the former direction input. World owns attached actors. Every enabled light
     * is disabled and world/material/pixelmaps are freed on every returning path;
     * native leaked the last three resources and only disabled the final light.
     * 1..9 lights are accepted; native debug diagnoses counts other than one. */
    int three_mm_model_prelight(const ThreeMMModelResource *, ThreeMMI32 lights, const ThreeMMModelLightingServices *);
#ifdef __cplusplus
}
#endif
#endif
