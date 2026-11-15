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
        int (*replace)(void *, const MMTextLocation *, int copy, MMI32 delete_bytes);
        MMI32 (*count)(void *, MMU32 kind, void *group);
        void *(*properties)(void *, void *group);
        int (*free_argument)(void *, void *group, MMI32 index);
        MMU32 (*fixed)(void *, void *group, MMI32 index);
        void (*put_fixed)(void *, void *group, MMI32 index, MMU32);
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
     * Collection access/publication and release callbacks never throw. */
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
