/* Model resource policy converted from engine modl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MODEL_RESOURCE_H
#define MM_ENGINE_MODEL_RESOURCE_H
#include "block_owner.h"
#include "body_geometry.h"
#include "read_stream.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    enum { MM_MODEL_HEADER_BYTES = 48, MM_MODEL_VERTEX_BYTES = 32, MM_MODEL_FACE_BYTES = 32 };
    typedef struct MMModelResource
    {
        void *handle;
    } MMModelResource;
    typedef struct MMModelDescription
    {
        MMI32 vertices, faces, radius;
        MMBodyBounds bounds;
        MMI32 pivot[3];
    } MMModelDescription;
    typedef struct MMModelFace
    {
        MMU32 vertices[3], edges[3], smoothing, flags, pad;
        MMI32 normal[3], distance;
    } MMModelFace;
    enum
    {
        MM_MODEL_VERTICES,
        MM_MODEL_FACES,
        MM_MODEL_VERTEX_GROUP,
        MM_MODEL_FACE_GROUP
    };
    enum
    {
        MM_MODEL_BAD_ORDER,
        MM_MODEL_BAD_COUNT,
        MM_MODEL_MULTIPLE_LIGHTS
    };
    typedef struct MMModelServices
    {
        void *context;
        int host_big_endian;
        MMU32 system;
        MMI32 face_bytes, vertex_group_bytes, face_group_bytes;
        unsigned prepared_flag, keep_original_flag, prepare_all;
        MMModelResource *(*state)(void *, void *object);
        void *(*create)(void *);
        void (*release)(void *, void *object);
        MMBlockTransform decode;
        int (*allocate)(void *, MMI32 bytes, int clear, void **out);
        void (*free_memory)(void *, void **);
        void (*invalidate)(void *, void *, size_t);
        void (*diagnostic)(void *, int);
        void (*save_failed)(void *);
        void *(*allocate_model)(void *, MMI32 vertices, MMI32 faces);
        void (*identify)(void *, void *handle, void *owner);
        void (*copy_input)(void *, void *handle, const void *vertices, const void *faces, MMI32, MMI32);
        void (*add)(void *, void *handle);
        void (*remove)(void *, void *handle);
        void (*free_model)(void *, void *handle);
        int (*component)(void *, void *handle, int kind, MMI32 bytes);
        void *(*vertex_data)(void *, void *handle, int prepared);
        void (*set_face)(void *, void *handle, int prepared, MMI32, const MMModelFace *);
        void (*get_face)(void *, void *handle, MMI32, MMModelFace *);
        void (*publish_prepared)(void *, void *handle, const MMModelDescription *, unsigned flag,
                                 MMI32 vertex_groups, MMI32 face_groups);
        void (*bind_group)(void *, void *handle, int face_group);
        void (*describe)(void *, void *handle, MMModelDescription *);
        void (*geometry)(void *, void *handle, const MMModelDescription *);
        int (*put)(void *, void *file, MMU32 type, MMU32 number, const void *, MMI32);
        void (*position)(void *, void *handle, MMI32, MMI32 out[3]);
        void (*set_position)(void *, void *handle, MMI32, const MMI32 in[3]);
        const void *(*path)(void *, void *filename);
        void *(*load_dat)(void *, const void *path);
        void (*or_flags)(void *, void *handle, unsigned flags);
        void (*prepare)(void *, void *handle, unsigned flags);
    } MMModelServices;
    /* Handles are renderer-owned allocations with lifetime controlled here. State
     * starts empty; do not reinitialize a nonempty owner. Direct read failure keeps
     * partial ownership; dispose releases it. Factory failure releases before
     * invalidating outputs, retaining native ordering and 32-bit cache estimates.
     * Read bounds are checked before native I/O. Negative signed wire counts fail
     * cleanly instead of invoking the original allocator/read assertions.
     * Callbacks are synchronous and must not reenter/mutate unrelated state. */
    void mm_model_init(MMModelResource *);
    void mm_model_dispose(MMModelResource *, const MMModelServices *);
    void *mm_model_new(MMI32, const void *, MMI32, const void *, const MMModelServices *);
    /* Borrow the block for the call. Factory estimates use the stored extent
     * without decoding. Actual reads decode the block in place. */
    int mm_model_fetch(MMOwnedBlock *, void *object_out, void *size_out, const MMModelServices *);
    int mm_model_read(void *object, MMOwnedBlock *, const MMModelServices *);
    int mm_model_write(const MMModelResource *, void *file, MMU32 type, MMU32 number,
                             const MMModelServices *);
    void *mm_model_dat(void *filename, const MMModelServices *);
    MMI32 mm_model_extent(const MMModelResource *, unsigned axis, const MMModelServices *);
    void mm_model_adjust_glyph(const MMModelResource *, const MMModelServices *);
    /* Wire faces are 32 bytes, with a discarded 32-bit material slot and zeroed
     * implicit padding on output. Vertices are opaque native-order 32-byte records.
     * The old reader normalized its header before testing bo again: foreign vertex
     * and face arrays therefore remain unswapped in both preparation branches. */
    int mm_model_header(const unsigned char[MM_MODEL_HEADER_BYTES], int host_big, MMModelDescription *, int *bad_order);
    void mm_model_face_read(const unsigned char[MM_MODEL_FACE_BYTES], int big, MMModelFace *);
    void mm_model_face_write(unsigned char[MM_MODEL_FACE_BYTES], int big, const MMModelFace *);

    typedef struct MMModelVertex
    {
        MMI32 position[3], mapping[2], normal[3];
        MMU32 reserved;
        unsigned char index, red, green, blue;
    } MMModelVertex;
    typedef struct MMModelMesh
    {
        MMModelDescription description;
        MMModelVertex *vertices;
        MMModelFace *faces;
    } MMModelMesh;
    enum { MM_MODEL_MESH_OK, MM_MODEL_MESH_BAD_FILE, MM_MODEL_MESH_IO, MM_MODEL_MESH_MEMORY };
    /* Read an exact-position stream over a decoded BMDL. A nonzero stored radius
     * marks preprepared geometry. Output is unchanged on failure and owns its
     * arrays on success. Payload byte order follows the legacy native reader. */
    int mm_model_mesh_read(const MMReadStream *, MMModelMesh *);
    void mm_model_mesh_dispose(MMModelMesh *);

    enum
    {
        MM_MODEL_LIGHT_WORLD,
        MM_MODEL_LIGHT_CAMERA,
        MM_MODEL_LIGHT_ACTOR,
        MM_MODEL_LIGHT_COLOUR,
        MM_MODEL_LIGHT_DEPTH,
        MM_MODEL_LIGHT_MATERIAL
    };
    typedef struct MMModelLightingSpec
    {
        MMU32 colour;
        MMI32 width, height, fov, hither, yon, aspect, camera_width, camera_height;
        MMI32 attenuation, ambient, diffuse, specular, power, index_base, index_range;
        int material_lit, material_gouraud;
    } MMModelLightingSpec;
    typedef struct MMModelLightingServices
    {
        void *context;
        void *(*create)(void *, int kind, const MMModelLightingSpec *);
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
    } MMModelLightingServices;
    /* Retains the native default camera/light/material and intentionally ignores
     * the former direction input. World owns attached actors. Every enabled light
     * is disabled and world/material/pixelmaps are freed on every returning path;
     * native leaked the last three resources and only disabled the final light.
     * 1..9 lights are accepted; native debug diagnoses counts other than one. */
    int mm_model_prelight(const MMModelResource *, MMI32 lights, const MMModelLightingServices *);
#ifdef __cplusplus
}
#endif
#endif
