#ifndef MARCUS_CAPTION_RUNS_H
#define MARCUS_CAPTION_RUNS_H

#include "player.h"
#include "mm/caption_render.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusCaptionInput
{
    MMTextBoxBounds box;
    const unsigned short *text;
    size_t length;
    MMI32 pixels;
    MMCaptionFont font;
    size_t properties;
    const void *property_context;
    void (*property)(const void *, size_t, MMCaptionProperty *);
} MarcusCaptionInput;

typedef struct MarcusCaptionMetrics
{
    MMI32 advance, ascent, cell_height, leading;
} MarcusCaptionMetrics;

typedef struct MarcusCaptionRaster
{
    MMCaptionMask geometry;
    const unsigned char *ink, *paper;
    MMI32 ascent;
    float density;
} MarcusCaptionRaster;

typedef struct MarcusCaptionMaskOwner
{
    MarcusCaptionRaster raster;
    size_t bytes;
    void *owner;
    void (*release)(void *);
} MarcusCaptionMaskOwner;

typedef struct MarcusCaptionRun
{
    MarcusCaptionMaskOwner mask;
    MMCaptionStyle style;
    unsigned short *text;
    MMI32 length, x, y, ascent, height;
} MarcusCaptionRun;

typedef struct MarcusCaptionRuns
{
    size_t *scene_bytes; /* Borrowed counter, shared by a scene and its draft. */
    size_t mask_bytes, count, stop_count;
    MMCollectionStorage storage;
    MMI32 height;
    int ready;
} MarcusCaptionRuns;

typedef struct MarcusCaptionFonts
{
    void *context;
    int (*face)(void *, const MMCaptionStyle *);
    int (*measure)(void *, const unsigned short *, MMI32, MarcusCaptionMetrics *);
    int (*mask)(void *, const unsigned short *, MMI32, MarcusCaptionMaskOwner *);
} MarcusCaptionFonts;

enum
{
    MARCUS_CAPTION_RUNS_BOUNDS = -3, MARCUS_CAPTION_RUNS_FONT = -4,
    MARCUS_CAPTION_RUNS_MEASURE = -5, MARCUS_CAPTION_RUNS_RASTER = -6,
    MARCUS_CAPTION_RUNS_CARET = -7, MARCUS_CAPTION_RUNS_BUDGET = -8,
    MARCUS_CAPTION_RUNS_MEMORY = -9
};

/* Start with a zeroed owner. An installed layout is reused until cleared.
 * Failure releases all staged masks and leaves the shared byte count intact.
 * Font services return one for success and zero for failure, with no unwind.
 * Successful mask construction transfers its owner. */
int marcus_caption_runs_layout(MarcusCaptionRuns *, const MarcusCaptionInput *, const MarcusCaptionFonts *,
                                size_t *scene_bytes, size_t budget, int editing);
void marcus_caption_runs_clear(MarcusCaptionRuns *);
const MarcusCaptionStop *marcus_caption_runs_stops(const MarcusCaptionRuns *);
int marcus_caption_runs_caret(const MarcusCaptionRuns *, MMCaptionPresentation *, MMI32 offset,
                             MMI32 top, MMI32 bottom);

/* A high-resolution raster is borrowed through the draw of the current run. */
typedef struct MarcusCaptionPaintServices
{
    void *context;
    MMU32 (*colour)(void *, MMU32 colour);
    int (*raster)(void *, const MarcusCaptionRun *, int scale, MarcusCaptionRaster *);
} MarcusCaptionPaintServices;
int marcus_caption_runs_paint(const MarcusCaptionRuns *, MMCaptionCanvas *, const MMTextBoxBounds *,
                               MMI32 scroll, MMU32 background, const MarcusCaptionPaintServices *);

#ifdef __cplusplus
}
#endif
#endif
