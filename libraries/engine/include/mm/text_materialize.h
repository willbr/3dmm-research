/* Stored rich text to live document ownership and argument normalization. */
#ifndef MM_ENGINE_TEXT_MATERIALIZE_H
#define MM_ENGINE_TEXT_MATERIALIZE_H
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
        int (*open_argument)(void *, MMI32 index, unsigned kind, int order, int system);
    } MMTextMaterializeServices;
    typedef struct MMTextFontArgumentServices
    {
        void *context;
        MMStringWire wire; /* Native string settings and conversion. C supplies the buffer. */
        int (*map)(void *, const MMMovieTitle *, int system, MMI32 *font);
        void (*bad_string)(void *);
        void (*bad_entry)(void *);
    } MMTextFontArgumentServices;
    /* Same synchronous exception contract as text_document.h. Base initialization
     * precedes stored reads. Defaults publish before translation; the translated
     * location's independent owner releases before property/argument transfers.
     * Failures retain published defaults, text, groups and normalized prefixes.
     * The legacy character count is passed unchanged as the replacement byte count.
     * The piece table is acquired after base initialization. The read services
     * return C-owned text collections. Argument hooks can edit their rows and
     * move buffer storage, while the published collection owner stays live.
     * Piece I/O, publication and release never throw. */
    int mm_text_materialize(const MMTextDocumentConfig *, MMU32 type, MMU32 number, int copy,
                            const MMTextMaterializeServices *);
    /* Looks up the entry before testing its kind. Font kind 2 uses the entry's
     * native short system. String diagnostics occur locked, then mapping runs
     * unlocked and the row is looked up again before publishing the mapped ID.
     * The argument collection remains live for the call. Conversion and
     * diagnostic callbacks never throw. */
    int mm_text_open_font_argument(MMTextCollection *, MMI32 index, unsigned kind, const MMTextFontArgumentServices *);
#ifdef __cplusplus
}
#endif
#endif
