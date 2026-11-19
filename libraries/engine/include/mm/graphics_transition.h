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

typedef enum MMGraphicsMovieIndexedOperation
{
    MM_MOVIE_TRANSITION_FILL_BLACK,
    MM_MOVIE_TRANSITION_SET_PALETTE,
    MM_MOVIE_TRANSITION_COPY,
    MM_MOVIE_TRANSITION_DISSOLVE
} MMGraphicsMovieIndexedOperation;

typedef struct MMGraphicsMoviePaletteSource
{
    const void *context;
    int (*read)(const void *, unsigned char bgrx[1024]);
    void (*overlay)(const void *, unsigned char bgrx[1024]);
} MMGraphicsMoviePaletteSource;

typedef struct MMGraphicsMovieSnapshot
{
    unsigned short *indices;
    unsigned char outgoing[1024], incoming[1024], target[1024];
} MMGraphicsMovieSnapshot;

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
/* Run Studio's indexed transition using a prepared palette and drawing port.
 * Return 1 on success, 0 for an unknown effect, or the host's negative error.
 * The callback returns zero on success and must not unwind through C. */
int mm_graphics_movie_transition_indexed(MMI32 authored_kind, int force_cut, void *context,
                                        int (*draw)(void *, MMGraphicsMovieIndexedOperation,
                                                    const MMGraphicsMovieTransition *));
/* Zero-initialize the owning snapshot. A successful prepare replaces it and
 * remaps the supplied outgoing image in place. Zero means no effect or invalid
 * image, and -1 means allocation failure. Both leave the snapshot and pixels
 * alone. Palette-source callbacks are optional and must not unwind through C. */
int mm_graphics_movie_snapshot_prepare(MMGraphicsMovieSnapshot *, MMI32 authored_kind,
                                      const MMPixelSurface *outgoing, const unsigned char *outgoing_palette,
                                      const MMGraphicsMoviePaletteSource *);
void mm_graphics_movie_snapshot_dispose(MMGraphicsMovieSnapshot *);
/* The outgoing surface is the same image used by prepare. Incoming indices,
 * when supplied, are tightly packed in the incoming surface's dimensions. */
int mm_graphics_movie_snapshot_render(const MMGraphicsMovieSnapshot *, MMI32 authored_kind,
                                     double elapsed_ms, MMU32 seed, const MMPixelSurface *outgoing,
                                     const MMPixelSurface *incoming, const unsigned char *incoming_indices);

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
