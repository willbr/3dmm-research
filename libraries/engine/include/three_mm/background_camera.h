/* Background camera/placement policy converted from engine/bkgd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BACKGROUND_CAMERA_H
#define THREE_MM_ENGINE_BACKGROUND_CAMERA_H
#include "actor_motion.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_BACKGROUND_CAMERA_BYTES = 76,
        THREE_MM_BACKGROUND_POINT_BYTES = 12
    };
    enum
    {
        THREE_MM_BACKGROUND_CAMERA_SHORT,
        THREE_MM_BACKGROUND_CAMERA_TAIL
    };
    typedef struct ThreeMMBackgroundCameraData
    {
        ThreeMMActorMatrix matrix;
        ThreeMMActorPoint place;
        ThreeMMI32 near_z, far_z, point_count;
        unsigned short fov;
        int big;
    } ThreeMMBackgroundCameraData;
    typedef struct ThreeMMBackgroundCameraState
    {
        ThreeMMI32 count, current, next_point, last_point;
        ThreeMMActorPoint place, position;
        ThreeMMActorMatrix mouse;
        unsigned short yaw;
        void *points;
    } ThreeMMBackgroundCameraState;
    typedef struct ThreeMMBackgroundCameraServices
    {
        void *context;
        void (*lights_on)(void *);
        int (*read)(void *, ThreeMMI32 camera, ThreeMMReadStream *, ThreeMMU32 *number);
        int (*child)(void *, ThreeMMU32 camera, int depth, ThreeMMU32 *number);
        int (*background)(void *, ThreeMMU32 rgb, ThreeMMU32 depth);
        void *(*new_points)(void *, ThreeMMI32 count);
        void (*release)(void *, void *), (*lock)(void *, void *), (*unlock)(void *, void *);
        void (*put_point)(void *, void *, ThreeMMI32, const ThreeMMActorPoint *);
        void (*point)(void *, void *, ThreeMMI32, ThreeMMActorPoint *);
        ThreeMMI32 (*point_count)(void *, void *);
        void (*euler_yxy)(void *, const ThreeMMActorMatrix *, unsigned short *a, unsigned short *c);
        void (*rotate_y)(void *, ThreeMMActorMatrix *, unsigned short yaw);
        void (*translate)(void *, ThreeMMActorMatrix *, const ThreeMMActorPoint *);
        void (*camera)(void *, const ThreeMMBackgroundCameraData *), (*dirty)(void *);
        void (*diagnostic)(void *, int issue);
    } ThreeMMBackgroundCameraServices;
    /* Exact 76+12*N layout, explicit wire endianness, no padding/pointer copies.
     * Decode failures leave output unchanged. Appended points decode independently
     * from their source offsets; no in-place access past a camera header. */
    int three_mm_background_camera_decode(const ThreeMMReadStream *, ThreeMMBackgroundCameraData *);
    int three_mm_background_point_decode(const ThreeMMReadStream *, const ThreeMMBackgroundCameraData *, ThreeMMI32,
                                         ThreeMMActorPoint *);
    void three_mm_background_camera_init(ThreeMMBackgroundCameraState *);
    /* Valid camera index and synchronous, non-reentrant services are required.
     * Lights turn on before reads; background image changes precede point replacement.
     * Failed point allocation falls back to the default placement and still succeeds.
     * Failed point reads discard the new list but retain earlier image/reset prefixes;
     * lock is balanced before release. Current camera/positions publish only after
     * point reads. Mouse math precedes world camera/dirty effects. State owns points. */
    int three_mm_background_camera_set(ThreeMMBackgroundCameraState *, ThreeMMI32,
                                       const ThreeMMBackgroundCameraServices *);
    /* Release the list and reset its cursors. Other metadata remains available. */
    void three_mm_background_camera_dispose(ThreeMMBackgroundCameraState *, const ThreeMMBackgroundCameraServices *);
    /* Cycle default, appended points, default; reuse resets next to last. */
    void three_mm_background_place(ThreeMMBackgroundCameraState *, ThreeMMActorPoint *,
                                   const ThreeMMBackgroundCameraServices *);
    void three_mm_background_reuse_place(ThreeMMBackgroundCameraState *);
#ifdef __cplusplus
}
#endif
#endif
