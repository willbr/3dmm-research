/* Background camera/placement policy converted from engine/bkgd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BACKGROUND_CAMERA_H
#define MM_ENGINE_BACKGROUND_CAMERA_H
#include "actor_motion.h"
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_BACKGROUND_CAMERA_BYTES = 76,
        MM_BACKGROUND_POINT_BYTES = 12,
        MM_BACKGROUND_CAMERA = 0x43414d20
    };
    enum
    {
        MM_BACKGROUND_CAMERA_SHORT,
        MM_BACKGROUND_CAMERA_TAIL
    };
    typedef struct MMBackgroundCameraData
    {
        MMActorMatrix matrix;
        MMActorPoint place;
        MMI32 near_z, far_z, point_count;
        unsigned short fov;
        int big;
    } MMBackgroundCameraData;
    enum { MM_BACKGROUND_HAS_COLOUR = 1, MM_BACKGROUND_HAS_DEPTH = 2 };
    typedef struct MMBackgroundCameraImages
    {
        MMU32 colour, depth;
        unsigned present;
    } MMBackgroundCameraImages;
    typedef struct MMBackgroundCameraBinding
    {
        MMBackgroundCameraData data;
        MMU32 number;
        MMBackgroundCameraImages images;
    } MMBackgroundCameraBinding;
    typedef int (*MMBackgroundCameraImageRead)(void *, MMU32 number, int depth, MMU32 *);
    typedef struct MMBackgroundCameraState
    {
        MMI32 count, current, next_point, last_point;
        MMActorPoint place, position;
        MMActorMatrix mouse;
        unsigned short yaw;
        void *points;
        MMBackgroundCameraBinding binding;
    } MMBackgroundCameraState;
    typedef struct MMBackgroundCameraServices
    {
        void *context;
        void (*lights_on)(void *);
        int (*read)(void *, MMI32 camera, MMReadStream *, MMU32 *number);
        MMBackgroundCameraImageRead child;
        int (*background)(void *, MMU32 rgb, MMU32 depth);
        void *(*new_points)(void *, MMI32 count);
        void (*release)(void *, void *), (*lock)(void *, void *), (*unlock)(void *, void *);
        void (*put_point)(void *, void *, MMI32, const MMActorPoint *);
        void (*point)(void *, void *, MMI32, MMActorPoint *);
        MMI32 (*point_count)(void *, void *);
        void (*euler_yxy)(void *, const MMActorMatrix *, unsigned short *a, unsigned short *c);
        void (*rotate_y)(void *, MMActorMatrix *, unsigned short yaw);
        void (*translate)(void *, MMActorMatrix *, const MMActorPoint *);
        void (*camera)(void *, const MMBackgroundCameraData *), (*dirty)(void *);
        void (*diagnostic)(void *, int issue);
    } MMBackgroundCameraServices;
    /* Exact 76+12*N layout, explicit wire endianness, no padding/pointer copies.
     * Decode failures leave output unchanged. Appended points decode independently
     * from their source offsets; no in-place access past a camera header. */
    int mm_background_camera_decode(const MMReadStream *, MMBackgroundCameraData *);
    /* Bind the selected CAM number to its decoded record. Link resolution
     * fills each image independently. A failed operation preserves output. */
    int mm_background_camera_binding_read(const MMReadStream *, MMU32 number, MMBackgroundCameraBinding *);
    int mm_background_camera_binding_image(MMBackgroundCameraBinding *, int depth, MMBackgroundCameraImageRead,
                                           void *context);
    int mm_background_point_decode(const MMReadStream *, const MMBackgroundCameraData *, MMI32,
                                         MMActorPoint *);
    /* Resolve the CAM's slot-zero MBMP or ZBMP in the same archive as the camera.
     * Returns a chunk status and publishes the linked number only on success. */
    int mm_background_camera_image_link(const MMChunkAccess *, MMU32 camera, int depth, MMU32 *number);
    void mm_background_camera_init(MMBackgroundCameraState *);
    /* Valid camera index and synchronous, non-reentrant services are required.
     * Lights turn on before reads; background image changes precede point replacement.
     * Failed point allocation falls back to the default placement and still succeeds.
     * Failed point reads discard the new list but retain earlier image/reset prefixes;
     * lock is balanced before release. Current camera/positions publish only after
     * point reads. Mouse math precedes world camera/dirty effects. State owns points. */
    int mm_background_camera_set(MMBackgroundCameraState *, MMI32,
                                       const MMBackgroundCameraServices *);
    /* Apply an evaluated CAM and read placement from its borrowed payload.
     * The caller turns on lights before evaluation. A null child service
     * requires both image links to be present in the binding. */
    int mm_background_camera_apply(MMBackgroundCameraState *, MMI32, const MMBackgroundCameraBinding *,
                                   const MMReadStream *, const MMBackgroundCameraServices *);
    /* Release the list and reset its cursors. Other metadata remains available. */
    void mm_background_camera_dispose(MMBackgroundCameraState *, const MMBackgroundCameraServices *);
    /* Cycle default, appended points, default; reuse resets next to last. */
    void mm_background_place(MMBackgroundCameraState *, MMActorPoint *,
                                   const MMBackgroundCameraServices *);
    void mm_background_reuse_place(MMBackgroundCameraState *);
#ifdef __cplusplus
}
#endif
#endif
