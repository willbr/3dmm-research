#ifndef MARCUS_ASM_WIRE_H
#define MARCUS_ASM_WIRE_H

#include "mm/movie_scene_journal.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ASM6 records are exchanged between little-endian x86 and x64 processes. */
enum { MARCUS_ASM_MAGIC = 0x364d5341, MARCUS_ASM_EDIT_DEFINITIONS = 256 };

typedef struct MarcusASMRequest
{
    MMU32 magic;
    MMI32 scene, frame;
    MMU32 width, height, flags;
    float yaw, pitch, zoom, roll, fov, near_clip, x, y, z;
    MMI32 orbit_object, display_mode, axis_view;
    MMU32 prop_count;
    MMI32 first_frame, last_frame;
    MMU32 scene_count, added_prop_count, camera_count;
    MMU32 edit_revision_low, edit_revision_high;
} MarcusASMRequest;

typedef struct MarcusASMCamera
{
    float x, y, z, yaw, pitch, move_speed, roll, vertical_fov;
} MarcusASMCamera;

typedef struct MarcusASMProjection
{
    unsigned char valid, reserved[3];
    float transform[12], vertical_fov, aspect, near_clip, far_clip, display_depth;
    unsigned char orthographic, reserved_ortho[3];
    float ortho_height;
} MarcusASMProjection;

typedef struct MarcusASMReply
{
    MMU32 magic, error_bytes, width, height, covered, depth;
    MarcusASMCamera camera;
    MarcusASMProjection projection, scene_camera;
} MarcusASMReply;

typedef struct MarcusASMOutput
{
    MarcusASMReply reply;
    const unsigned char *rgba;
    const float *depth;
    size_t rgba_bytes, depth_bytes;
} MarcusASMOutput;

struct MarcusPlayer;
typedef struct MarcusASMWorkerServices
{
    void *context;
    void (*begin)(void *);
    int (*edits)(void *, const MarcusASMRequest *, const MMPreviewLiveScene *, const MMPreviewLiveProp *,
                 const MMMovieCameraEdit *);
    int (*range)(void *, MMI32 first, MMI32 last);
    int (*placements)(void *, const MMMovieActorPlacement *, MMU32 count);
    int (*render)(void *, const MarcusASMRequest *, MarcusASMOutput *);
    const char *(*error)(void *, size_t *bytes);
} MarcusASMWorkerServices;

/* Streams are binary. Services borrow the packet for the current request and
 * keep image data valid through the reply. Callbacks must not unwind through C. */
int marcus_asm_worker_run(struct MarcusPlayer *, FILE *input, FILE *output, const MarcusASMWorkerServices *);

#ifdef __cplusplus
}
#endif
#endif
