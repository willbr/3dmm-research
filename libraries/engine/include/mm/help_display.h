/* Help embedded-object geometry and drawing sequence. */
#ifndef MM_ENGINE_HELP_DISPLAY_H
#define MM_ENGINE_HELP_DISPLAY_H
#include "help_text.h"
#include "masked_bitmap.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMHelpDisplay
    {
        int hide_buttons;
    } MMHelpDisplay;
    typedef struct MMHelpDisplayServices
    {
        void *context;
        MMGroup (*arguments)(void *);
        int (*find)(void *, const MMTextChunk *, void **borrowed);
        int (*child)(void *, void *borrowed, const MMTextChunk *, MMU32 id, MMTextChunk *);
        int (*fetch)(void *, void *borrowed, const MMTextChunk *, void **owned);
        int (*rectangle)(void *, void *bitmap, MMMaskedRect *);
        void (*release)(void *, void *bitmap);
        int (*font)(void *), (*measure)(void *, MMI32 baseline, MMMaskedRect *);
        MMU32 (*color)(void *, int foreground), (*style)(void *);
        MMI32 (*x)(void *);
        void (*publish_x)(void *, MMI32);
        int (*fill)(void *, const MMMaskedRect *, MMU32 color);
        int (*draw)(void *, void *bitmap, const MMMaskedRect *);
        int (*pen)(void *);
        int (*frame)(void *, const MMMaskedRect *, int patterned, MMU32 foreground);
    } MMHelpDisplayServices;
    /* Valid native indices/coordinates. Only kind 192 is handled; the first eight
     * argument bytes are decoded in native order. Button lookup uses bitmap child
     * 0x10000. Bounds preserve failure output; bitmap bounds align left/bottom to zero.
     * Drawing uses the graphics environment's clip, independent of the caller clip.
     * Hidden buttons still resolve, measure, fill, frame and advance. */
    void mm_help_display_init(MMHelpDisplay *);
    void mm_help_display_hide(MMHelpDisplay *, int hidden);
    int mm_help_object_bounds(MMI32 index, unsigned kind, int big, MMMaskedRect *, const MMHelpDisplayServices *);
    /* Bitmap release precedes boxing and final x read/publication. Edit controls
     * publish x before pen/frame/fill. Style/colors/x are read at their native stages.
     * Synchronous fallible services return 1/0 or -1/-2, never throwing across C;
     * primitive header/color/style/x access, publication and release never throw.
     * Private bitmap owners release on all exits, including exception results; other
     * partial graphics/output effects remain without rollback. Arithmetic wraps at
     * 32 bits to preserve the retail coordinate model. */
    int mm_help_object_draw(const MMHelpDisplay *, MMI32 index, unsigned kind, MMI32 baseline, int big,
                            const MMHelpDisplayServices *);
#ifdef __cplusplus
}
#endif
#endif
