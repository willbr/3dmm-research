#ifndef MM_ENGINE_CAPTION_LAYOUT_H
#define MM_ENGINE_CAPTION_LAYOUT_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMCaptionFont
{
    const char *name;
    size_t length;
} MMCaptionFont;

typedef struct MMCaptionProperty
{
    MMU32 position, kind, value;
    MMCaptionFont font;
} MMCaptionProperty;

typedef struct MMCaptionStyle
{
    MMCaptionFont font;
    MMI32 pixels, flags, offset;
    MMU32 foreground, background;
} MMCaptionStyle;

typedef struct MMCaptionLayoutInput
{
    const unsigned short *text;
    MMI32 length, width, pixels;
    MMCaptionFont font;
    size_t properties;
} MMCaptionLayoutInput;

typedef struct MMCaptionLayoutRun
{
    const unsigned short *text;
    MMI32 first, drawn, end;
    MMI32 x, right, top, height;
} MMCaptionLayoutRun;

typedef struct MMCaptionLayoutServices
{
    void *context;
    void (*property)(void *, size_t, MMCaptionProperty *);
    int (*set_font)(void *, const MMCaptionStyle *);
    int (*measure)(void *, const unsigned short *, MMI32 count, MMI32 *width, MMI32 *top, MMI32 *bottom);
    int (*emit)(void *, const MMCaptionStyle *, const unsigned short *, MMI32 count, MMI32 x, MMI32 baseline);
    /* Optional coordinates for editor adornments, including tabs and empty rows. */
    int (*position)(void *, const MMCaptionLayoutRun *);
} MMCaptionLayoutServices;

enum { MM_CAPTION_LAYOUT_FAILED = -1, MM_CAPTION_LAYOUT_STALLED = -2 };
/* The UTF-16 input excludes the implicit final paragraph break. Properties
 * are read in authored order. Borrowed font names stay valid during the call.
 * Services return zero on success or a negative error and never unwind through
 * C. A run's characters are valid during emit. Height is written on success. */
int mm_caption_layout(const MMCaptionLayoutInput *, const MMCaptionLayoutServices *, MMI32 *height);

#ifdef __cplusplus
}
#endif
#endif
