/* Stored rich text to live document ownership and argument normalization. */
#ifndef MM_ENGINE_TEXT_MATERIALIZE_H
#define MM_ENGINE_TEXT_MATERIALIZE_H
#include "piece_table.h"
#include "text_document.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMTextMaterializeServices
    {
        void *context;
        MMRichTextReadServices read;
        MMTextBaseServices base;
        int (*init_base)(void *);
        int (*defaults)(void *, const MMRichTextDefaults *, const MMMovieTitle *);
        MMPieceTable *(*pieces)(void *);
        MMArray (*properties)(void *group);
        MMGroup (*arguments)(void *group);
        int (*open_argument)(void *, MMI32 index, unsigned kind, int order, int system);
    } MMTextMaterializeServices;
    typedef struct MMTextFontArgumentServices
    {
        void *context;
        MMI32 (*bytes)(void *, MMI32 index);
        int (*system)(void *, MMI32 index);
        const void *(*lock)(void *, MMI32 index);
        void (*unlock)(void *);
        int (*decode)(void *, const void *, MMI32 bytes, MMMovieTitle *);
        int (*map)(void *, const MMMovieTitle *, int system, MMI32 *font);
        void (*put)(void *, MMI32 index, MMI32 font);
        void (*diagnostic)(void *);
    } MMTextFontArgumentServices;
    /* Same synchronous exception contract as text_document.h. Base initialization
     * precedes stored reads. Defaults publish before translation; the translated
     * location's independent owner releases before property/argument transfers.
     * Failures retain published defaults, text, groups and normalized prefixes.
     * The legacy character count is passed unchanged as the replacement byte count.
     * The piece table is acquired after base initialization. Collection views
     * borrow the published owners and are refreshed after virtual argument hooks.
     * Piece I/O and collection access, publication and release never throw. */
    int mm_text_materialize(const MMTextDocumentConfig *, MMU32 type, MMU32 number, int copy,
                            const MMTextMaterializeServices *);
    /* Queries size even for unknown kinds. Font kind 2 uses the entry's native short
     * system (not the collection encoding/order). Decode diagnostics occur locked;
     * mapping occurs unlocked. Decode exceptions unlock without a diagnostic. */
    int mm_text_open_font_argument(unsigned unit, MMI32 index, unsigned kind, const MMTextFontArgumentServices *);
#ifdef __cplusplus
}
#endif
#endif
