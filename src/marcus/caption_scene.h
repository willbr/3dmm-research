#ifndef MARCUS_CAPTION_SCENE_H
#define MARCUS_CAPTION_SCENE_H

#include "caption_runs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusCaptionSource
{
    MarcusCaptionInput layout;
    MMI32 first, end;
    MMU32 chunk, background;
    int story;
} MarcusCaptionSource;

typedef struct MarcusStoredCaption
{
    MarcusCaptionSource source;
    MMCaptionProperty *properties;
    MarcusCaptionRuns runs;
} MarcusStoredCaption;

typedef struct MarcusCaptionScene
{
    MMCaptionPresentation presentation;
    MarcusStoredCaption *rows, *draft;
    size_t count, capacity, mask_bytes;
} MarcusCaptionScene;

typedef int MarcusCaptionRead(const void *, size_t index, MarcusCaptionSource *);

int marcus_caption_scene_init(MarcusCaptionScene *);
void marcus_caption_scene_dispose(MarcusCaptionScene *);
void marcus_caption_scene_clear(MarcusCaptionScene *);
/* Copy authored descriptors, text and formatting in scene order. The reader
 * lends its data through installation. Failure preserves the previous scene. */
int marcus_caption_scene_load(MarcusCaptionScene *, size_t count, const void *, MarcusCaptionRead *);
struct MMPreviewOwnedCaption;
/* Take an authored caption during scene staging. Successful transfer zeros the
 * source, and failure leaves ownership with the decoder. */
int marcus_caption_scene_take(MarcusCaptionScene *, struct MMPreviewOwnedCaption *);
int marcus_caption_scene_publish(MarcusCaptionScene *, MMI32 frame);
void marcus_caption_scene_clear_draft(MarcusCaptionScene *);
/* Replace the draft and lay it out against the scene's mask budget. A failed
 * draft leaves the authored rows intact and clears the draft preview. */
int marcus_caption_scene_draft(MarcusCaptionScene *, const MarcusCaptionSource *, const MarcusCaptionFonts *);
int marcus_caption_scene_layout(MarcusCaptionScene *, MarcusStoredCaption *, const MarcusCaptionFonts *);
int marcus_caption_scene_measure(const MarcusCaptionSource *, const MarcusCaptionFonts *, MMI32 *height);
MarcusStoredCaption *marcus_caption_scene_row(MarcusCaptionScene *, size_t row);

#ifdef __cplusplus
}
#endif
#endif
