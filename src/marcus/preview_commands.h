#ifndef MARCUS_PREVIEW_COMMANDS_H
#define MARCUS_PREVIEW_COMMANDS_H

#include "player.h"
#include "preview_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MARCUS_PREVIEW_END = 0, MARCUS_PREVIEW_REQUEST = 1, MARCUS_PREVIEW_INVALID = -1 };

/* Readers return END when formatted extraction stops. A value outside the
 * target field may consume a full command and still receive an error reply. */
typedef int (*MarcusPreviewPositionRead)(void *, int *scene, long *frame, int *fits);
int marcus_preview_run_frames(FILE *, MarcusPlayer *, int initial_scene, int cameras,
                              void *input, MarcusPreviewPositionRead);

typedef struct MarcusThumbnailView
{
    float yaw, pitch, zoom;
    MMU32 width, height;
} MarcusThumbnailView;

typedef struct MarcusPreviewTextRequest
{
    const char *load_path;
    MarcusThumbnailView view;
} MarcusPreviewTextRequest;

typedef struct MarcusPreviewModelServices
{
    void *context;
    int (*read)(void *, MMU32 *chunk, int *fits);
    int (*render)(void *, MMU32 chunk, const MarcusThumbnailView *, MarcusPreviewPacket *);
    const char *(*error)(void *);
} MarcusPreviewModelServices;

typedef struct MarcusPreviewTextServices
{
    void *context;
    int (*read)(void *, MarcusPreviewTextRequest *);
    int (*load)(void *, const char *path);
    int (*render)(void *, const MarcusThumbnailView *, MarcusPreviewPacket *);
    const char *(*error)(void *);
} MarcusPreviewTextServices;

/* Renderer adapters return 1 on success, 0 on a render/load failure and -1
 * on a native exception. Borrowed storage stays valid until the next request. */
int marcus_preview_run_models(FILE *, const MarcusPreviewModelServices *);
int marcus_preview_run_text(FILE *, const MarcusPreviewTextServices *);

#ifdef __cplusplus
}
#endif
#endif
