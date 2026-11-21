/* Plain/rich text replacement transactions over piece tables and virtual services. */
#ifndef MM_ENGINE_TEXT_REPLACE_H
#define MM_ENGINE_TEXT_REPLACE_H
#include "text_format.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TEXT_REPLACE_BYTES,
        MM_TEXT_REPLACE_LOCATION,
        MM_TEXT_REPLACE_STREAM
    };
    enum
    {
        MM_TEXT_REPLACE_BAD_LOCATION,
        MM_TEXT_REPLACE_BAD_SOURCE,
        MM_TEXT_REPLACE_PARA_BEFORE,
        MM_TEXT_REPLACE_PARA_AFTER
    };
    typedef struct MMTextReplaceRequest
    {
        int kind, copy, system;
        const void *data;
        MMTextLocation location;
        void *stream;
        MMI32 source, inserted, at, deleted;
        MMU32 flags;
    } MMTextReplaceRequest;
    typedef struct MMTextReplaceBaseServices
    {
        void *context;
        MMTextDocumentObjects objects;
        int (*translate)(void *, MMTextLocation *owned, int system);
        MMPieceTable *(*pieces)(void *stream);
        int (*invalidate)(void *, MMI32 at, MMI32 inserted, MMI32 deleted);
        void (*validate)(void *);
        int (*notify)(void *, MMI32 at, MMI32 inserted, MMI32 deleted, MMU32 flags);
    } MMTextReplaceBaseServices;
    typedef struct MMTextReplaceRichServices
    {
        void *context;
        MMTextReplaceBaseServices base;
        MMTextFormatEditServices edit;
        int has_character, has_paragraph;
        MMI32 (*characters)(void *);
        int (*replace_base)(void *, const MMTextReplaceRequest *);
        int (*replace_virtual)(void *, const MMTextReplaceRequest *);
        int (*adjust)(void *, MMI32 at, MMI32 inserted, MMI32 deleted);
        void (*validate_apply)(void *, MMI32 at, MMI32 span);
        void (*too_much)(void *), (*diagnostic)(void *, int);
        int (*notify)(void *, MMI32 at, MMI32 inserted, MMI32 deleted, MMU32 flags);
    } MMTextReplaceRichServices;
    /* Native units are 1/2 bytes, with valid source/destination ranges. Locations
     * acquire an independent mutable file reference and release its final owner on
     * every exit. Base locations release after cache/validation/notification effects;
     * rich success releases its translated view before map adjustment/undo commit.
     * Ordinary failures cancel only established undo, retaining prior dirty/text effects.
     * Negative native exception results retain partial edit/reference effects without
     * added cancellation/rollback, while releasing owned file locations. */
    int mm_text_replace_base(unsigned unit, const MMTextReplaceRequest *, const MMTextReplaceBaseServices *);
    int mm_text_replace_rich(unsigned unit, int system, const MMTextReplaceRequest *,
                             const MMTextReplaceRichServices *);
    /* Formatted replacement generates deltas before undo, reserves before setup,
     * dispatches through virtual unformatted replacement, then applies its batches.
     * Paragraph batches use contained old/new ranges and expanded undo extents.
     * These services use the same synchronous exception contract as text_format.h. */
    int mm_text_replace_formatted(unsigned unit, int system, const MMTextReplaceRequest *,
                                  const MMTextReplaceRichServices *);
    /* The legacy formatted-location wrapper forwards the original BYTE count as
     * the core insertion count, even after translation; it is not silently repaired.
     * Copying is disabled if this wrapper's translation replaces the source owner. */
    int mm_text_replace_formatted_location(unsigned unit, int system, const MMTextReplaceRequest *,
                                           const MMTextReplaceRichServices *);
#ifdef __cplusplus
}
#endif
#endif
