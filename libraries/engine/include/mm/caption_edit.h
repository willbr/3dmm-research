#ifndef MM_ENGINE_CAPTION_EDIT_H
#define MM_ENGINE_CAPTION_EDIT_H
#include "text.h"
#include "text_properties.h"
#include "text_store.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { MM_CAPTION_STYLE = 1, MM_CAPTION_FOREGROUND = 3, MM_CAPTION_ALIGNMENT = 128 };
enum
{
    MM_CAPTION_EDIT_OK, MM_CAPTION_EDIT_SIZE, MM_CAPTION_EDIT_POSITION, MM_CAPTION_EDIT_RANGE,
    MM_CAPTION_EDIT_FONT_SIZE, MM_CAPTION_EDIT_ALIGNMENT, MM_CAPTION_EDIT_MEMORY,
    MM_CAPTION_EDIT_ADJUST, MM_CAPTION_EDIT_INHERIT
};
enum { MM_CAPTION_EDIT_SET_SIZE = 1, MM_CAPTION_EDIT_SET_PAPER = 2, MM_CAPTION_EDIT_SET_WIDTH = 4 };

typedef struct MMCaptionEditOptions
{
    MMI32 x, y, width, height, first, last, through_end, scrolling;
    MMI32 font_size, bold, italic, alignment, transparent;
    unsigned long ink, paper;
} MMCaptionEditOptions;

typedef struct MMCaptionEditFormat
{
    unsigned defaults;
    MMI32 paper, count;
    MMTextPropertyChange changes[3];
} MMCaptionEditFormat;

/* The editor range is inclusive and one-based. A text box stores authored
 * frames with an exclusive end. An unchanged range keeps its authored values. */
int mm_caption_edit_read_box(const MMTextBoxState *, MMI32 scene_first, MMI32 scene_last, MMCaptionEditOptions *);
int mm_caption_edit_write_box(MMTextBoxState *, MMI32 scene_first, MMI32 scene_last,
                               const MMCaptionEditOptions *, const MMCaptionEditOptions *before);
int mm_caption_edit_format(const MMCaptionEditOptions *, const MMCaptionEditOptions *before, MMCaptionEditFormat *);

/* CR, LF and CRLF each become one separator. Offsets use UTF-16 code units. */
size_t mm_caption_edit_normalize(unsigned short *text, size_t count, unsigned short separator);
enum { MM_CAPTION_EDIT_MAX_UNITS = 65536 };
int mm_caption_edit_decode(const char *utf8, size_t length, unsigned short *, size_t capacity, size_t *count);
/* Buffer capacity must cover two bytes per unit. Returns the stored encoding
 * or zero on failure. ANSI is used only for a one-byte-per-unit round trip. */
int mm_caption_edit_encode(const MMTextHost *, const unsigned short *, size_t count,
                           unsigned char *, size_t capacity, size_t *bytes);
MMI32 mm_caption_edit_paragraph_end(const unsigned short *text, MMI32 count, MMI32 start);

/* Adjust a private property map for the changed span between two texts.
 * The paragraph provider reads the proposed text. Diagnostic callbacks set
 * *diagnosed and the caller discards the private map if the edit fails. */
int mm_caption_edit_replace(const unsigned short *old_text, MMI32 old_count,
                            const unsigned short *new_text, MMI32 new_count,
                            const MMTextPropertyServices *, const int *diagnosed);

#ifdef __cplusplus
}
#endif
#endif
