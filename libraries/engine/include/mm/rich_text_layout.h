/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_RICH_TEXT_LAYOUT_H
#define MM_ENGINE_RICH_TEXT_LAYOUT_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_RICH_WHITE = 1, MM_RICH_BREAK = 2, MM_RICH_MAY_BREAK = 4,
       MM_RICH_IGNORE = 8, MM_RICH_TAB = 32, MM_RICH_BUFFER = 128 };
typedef struct MMRichTextParagraph
{
    MMI32 justification, indent, tab, lineScale, afterScale, extraLine, extraAfter;
} MMRichTextParagraph;
typedef struct MMRichTextRunData
{
    MMI32 cpLim, cpLimDraw, xpLim, xpLimDraw;
} MMRichTextRunData;
typedef struct MMRichTextLine
{
    MMI32 cpMin, dypTot, ccp, xpLeft, dyp, dypAscent;
} MMRichTextLine;
typedef enum MMRichTextOperation
{
    MM_RICH_COPY_STYLE, MM_RICH_SET_FONT, MM_RICH_WARM_FONT,
    MM_RICH_MEASURE, MM_RICH_OFFSET, MM_RICH_FLAGS, MM_RICH_LENGTH,
    MM_RICH_PARAGRAPH_START, MM_RICH_FETCH, MM_RICH_OBJECT,
    MM_RICH_FETCH_STYLE, MM_RICH_FETCH_PARAGRAPH, MM_RICH_PUBLISH,
    MM_RICH_INVALID_STYLE
} MMRichTextOperation;
typedef struct MMRichTextRequest
{
    MMI32 cp, count, value, left, right, top, bottom;
    MMU32 character, flags;
    void *characters;
    MMRichTextParagraph paragraph;
    MMRichTextLine line;
    unsigned published; /* 1: xpLeft/ccp/dyp; 2: all remaining line fields */
} MMRichTextRequest;
typedef struct MMRichTextProvider
{
    void *context;
    /* Return zero on success, negative on failure. Boolean results use value.
     * COPY_STYLE snapshots the host's fetched style into a mutable run style.
     * OBJECT may mutate that run style, even when it returns value == 0.
     * No callback may unwind through C. Buffers contain native 1/2-byte units.
     * Request inputs -> outputs (leave all other fields unchanged):
     * MEASURE: characters/count -> left/right/top/bottom;
     * OFFSET, LENGTH: no inputs -> value;
     * FLAGS: character -> flags; PARAGRAPH_START: cp -> value;
     * FETCH: cp/count -> contents of characters;
     * OBJECT: cp -> value and, on a hit, left/right/top/bottom;
     * FETCH_STYLE: cp -> value (exclusive end), plus host's fetched style;
     * FETCH_PARAGRAPH: cp -> paragraph/value (exclusive end);
     * PUBLISH: line/published -> host output; INVALID_STYLE: cp -> diagnostic.
     * COPY_STYLE, SET_FONT and WARM_FONT have no request fields. */
    int (*call)(void *, MMRichTextOperation, MMRichTextRequest *);
    void *characters; /* storage for at least 128 character units */
    unsigned unitBytes;
} MMRichTextProvider;
typedef struct MMRichTextRun
{
    MMRichTextProvider provider;
    MMRichTextParagraph paragraph;
    int mustAdvance, lineBreak, object;
    MMI32 cpMin, cpLim, cpLimFetch, xpMin, xpBreak, ascent, descent;
    MMRichTextRunData data, opportunity;
} MMRichTextRun;
MMU32 mm_rich_text_flags(MMU32 character);
/* Zero-initialize a new run. Reinitialization deliberately retains the object
 * flag until the next nonempty run, matching the original formatter. Return
 * zero on success or the provider's negative error; partial state is retained. */
int mm_rich_text_run_init(MMRichTextRun *, const MMRichTextProvider *,
                         const MMRichTextParagraph *, MMI32 cpMin, MMI32 cpLim,
                         MMI32 xpBase, MMI32 width, MMI32 xpBreak);
int mm_rich_text_run_next(MMRichTextRun *, int mustAdvance);
/* PUBLISH exposes the first three output fields before the final paragraph
 * query, then publishes the complete line. The provider owns the output. */
int mm_rich_text_line(const MMRichTextProvider *, MMI32 width, MMI32 cpMin, MMI32 y);
#ifdef __cplusplus
}
#endif
#endif
