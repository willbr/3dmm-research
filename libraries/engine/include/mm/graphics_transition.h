#ifndef MM_ENGINE_GRAPHICS_TRANSITION_H
#define MM_ENGINE_GRAPHICS_TRANSITION_H
#include "graphics_view.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum MMGraphicsTransitionKind
{
    MM_GRAPHICS_WIPE,
    MM_GRAPHICS_SLIDE,
    MM_GRAPHICS_FADE,
    MM_GRAPHICS_IRIS,
    MM_GRAPHICS_DISSOLVE
} MMGraphicsTransitionKind;

typedef enum MMGraphicsPalettePhase
{
    MM_GRAPHICS_PALETTE_BLEND,
    MM_GRAPHICS_PALETTE_TO_FILL,
    MM_GRAPHICS_PALETTE_FROM_FILL
} MMGraphicsPalettePhase;

typedef struct MMGraphicsTransition
{
    MMGraphicsTransitionKind kind;
    MMU32 duration, directions;
    MMI32 fade_steps;
    int fill, palette;
    MMPixelRect source, destination;
    const MMGraphicsView *view;
    MMPixelPoint focus, port_base;
    MMPixelPoint dissolve_cells;
    int has_source;
} MMGraphicsTransition;

typedef struct MMGraphicsDissolvePixels
{
    MMPixelSurface destination, source;
    MMPixelPoint source_origin;
} MMGraphicsDissolvePixels;

enum MMGraphicsMovieTransitionKind
{
    MM_GRAPHICS_MOVIE_CUT = 0,
    MM_GRAPHICS_MOVIE_FADE_BLACK = 1,
    MM_GRAPHICS_MOVIE_FADE_WHITE = 2,
    MM_GRAPHICS_MOVIE_DISSOLVE = 3,
    MM_GRAPHICS_MOVIE_BLACK = 4
};

typedef struct MMGraphicsMovieTransition
{
    MMU32 dissolve_phase_ms;
    MMI32 fill; /* A grayscale level, or -1 for a direct dissolve. */
    double duration_ms;
} MMGraphicsMovieTransition;

typedef struct MMGraphicsMovieDissolvePalette
{
    const unsigned char *outgoing, *incoming; /* Complete BGRX display palettes. */
    const unsigned char *target;             /* Outgoing base with raw authored entries overlaid. */
    const unsigned short *outgoing_indices;   /* Remapped snapshot, including unlisted pixels. */
    const unsigned char *incoming_indices;   /* Optional raster indices as colour lookup hints. */
} MMGraphicsMovieDissolvePalette;

typedef struct MMGraphicsTransitionServices
{
    void *context;
    MMU32 (*ticks)(void *);
    void (*flush)(void *);
    /* Prepare supplies a slide snapshot or a reusable native iris clip. */
    int (*prepare)(void *, MMGraphicsTransitionKind);
    int (*depth)(void *);
    int (*start_palette)(void *, int depth);
    void (*animate_palette)(void *, MMGraphicsPalettePhase, MMU32 step, MMU32 total);
    void (*set_palette)(void *, int original_if_unspecified);
    void (*draw)(void *, int snapshot, int fill, const MMPixelRect *source, const MMPixelRect *destination);
    void (*scroll)(void *, MMPixelPoint);
    void (*present)(void *);
    /* Draw and flush under the temporary clip before restoring the port. */
    int (*iris)(void *, int fill, const MMPixelRect *include, const MMPixelRect *exclude);
    /* Pixel dissolve borrows an 8-bit snapshot and, if supplied, an 8-bit
     * source. Unlock is called after every lock attempt, including failure. */
    int (*lock_dissolve)(void *, MMGraphicsDissolvePixels *);
    void (*unlock_dissolve)(void *);
    MMU32 (*random_below)(void *, MMU32 exclusive_limit);
    /* Optional device submission after the dissolve snapshot has been flushed. */
    void (*show_dissolve)(void *);
} MMGraphicsTransitionServices;

/* The clock uses wrapping millisecond words. Host services must stay valid
 * for the synchronous transition. Iris requires the destination view. */
MMU32 mm_graphics_transition_duration(MMU32);
void mm_graphics_transition_run(const MMGraphicsTransition *, const MMGraphicsTransitionServices *);

MMGraphicsMovieTransition mm_graphics_movie_transition(MMI32 authored_kind);
/* Update an incoming RGBA8 image in place from a separate outgoing image.
 * A stable seed gives the same reveal on repeated presentations. Optional
 * dissolve palette indices are tightly packed in each surface's dimensions. */
int mm_graphics_movie_transition_rgba(MMI32 authored_kind, double elapsed_ms, MMU32 seed,
                                      const MMPixelSurface *outgoing, const MMPixelSurface *incoming,
                                      const MMGraphicsMovieDissolvePalette *);

/* The four-byte colours are BGR and a reserved byte. Missing palettes use
 * the substitute or zero. Preserve the old colour's reserved byte. */
void mm_graphics_transition_palette(unsigned char *out, MMI32 count,
                                   const unsigned char *old, MMI32 old_count,
                                   const unsigned char *next, MMI32 next_count,
                                   const unsigned char *substitute, MMU32 step, MMU32 total);

#ifdef __cplusplus
}
#endif
#endif
