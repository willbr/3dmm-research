/* Character/paragraph formatting, cached ranges and apply transactions. */
#ifndef MM_ENGINE_TEXT_FORMAT_H
#define MM_ENGINE_TEXT_FORMAT_H
#include "text_properties.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TEXT_FORMAT_FAILED = -3,
        MM_TEXT_FONT_RECORD = 522
    };
    enum
    {
        MM_TEXT_PARA_START,
        MM_TEXT_PARA_END,
        MM_TEXT_PARA_IS_START,
        MM_TEXT_PARA_NEXT
    };
    typedef struct MMTextCharacter
    {
        MMU32 style;
        MMI32 font, height, offset;
        MMU32 foreground, background;
    } MMTextCharacter;
    typedef struct MMTextParagraph
    {
        unsigned justification, indent;
        int tab, line, extra_line, after, extra_after;
    } MMTextParagraph;
    typedef struct MMTextCharacterCache
    {
        MMTextCharacter value;
        MMI32 first, end;
    } MMTextCharacterCache;
    typedef struct MMTextParagraphCache
    {
        MMTextParagraph value;
        MMI32 first, end;
    } MMTextParagraphCache;
    typedef struct MMTextFormatConfig
    {
        MMTextDocumentConfig text;
        MMI32 font, height;
        MMU32 black, clear;
        int tab, line, after, signed_offset;
    } MMTextFormatConfig;
    typedef struct MMTextFormatServices
    {
        void *context;
        MMTextPropertyServices properties;
        int (*font_name)(void *, MMI32 font, MMMovieTitle *);
        int (*valid_font)(void *, MMI32 font);
        void (*bad_font_record)(void *);
    } MMTextFormatServices;
    typedef struct MMTextParagraphServices
    {
        void *context;
        int (*query)(void *, int kind, MMI32 cp, MMI32 *out);
    } MMTextParagraphServices;
    /* Delta returns 1/0 for change/no change, -3 for ordinary font-record/intern
     * failure, or -1/-2 for native exceptions. Mask is always published; value is
     * unchanged on no change/failure. Font records use native ID/system followed
     * by the shared native counted-string codec. Config supplies historical defaults
     * and achar byte signedness; paragraph fields retain signed-short wrapping. */
    int mm_text_character_delta(unsigned kind, const MMTextFormatConfig *, const MMTextCharacter *value,
                                const MMTextCharacter *old, MMU32 *word, MMU32 *mask, const MMTextFormatServices *);
    int mm_text_paragraph_delta(unsigned kind, const MMTextFormatConfig *, const MMTextParagraph *value,
                                const MMTextParagraph *old, MMU32 *word, MMU32 *mask);
    /* Caller supplies four character or three paragraph slots. Count records the
     * emitted prefix on all exits. Ordinary failure releases that prefix's references;
     * exceptions retain partial effects, like the native path. */
    int mm_text_format_changes(int paragraph, const MMTextFormatConfig *, const void *value, const void *old,
                               MMTextPropertyChange *, MMI32 *count, const MMTextFormatServices *);
    int mm_text_fetch_character(const MMTextFormatConfig *, MMTextCharacterCache *, MMI32 cp, MMI32 characters,
                                const MMTextFormatServices *);
    void mm_text_fetch_paragraph(const MMTextFormatConfig *, MMTextParagraphCache *, MMI32 cp, MMI32 characters,
                                 const MMTextFormatServices *);
    /* Queries are synchronous and return 1/0 or -1/-2, never throwing across C.
     * Cache/range mutations preceding a failure remain published. Cache hits do not
     * consult property/font services. Native warnings for malformed font-record sizes
     * keep the default font and continue. Collection/diagnostic callbacks never throw. */
    int mm_text_paragraph_bounds(MMI32 *first, MMI32 *end, MMI32 characters, int expand,
                                 const MMTextParagraphServices *);
    typedef struct MMTextFormatEditServices
    {
        void *context;
        MMTextFormatServices format;
        MMTextParagraphServices paragraphs;
        /* Optional native marshalling after virtual undo setup. Overrides the
         * config/value/old arguments supplied to apply when present. */
        void (*read_input)(void *, int paragraph, MMTextFormatConfig *, void *value, void *old, int *has_old);
        void (*publish_range)(void *, MMI32 first, MMI32 end);
        void (*bump)(void *), (*validate)(void *);
        MMI32 (*characters)(void *); /* Optional current stream length after reserve. */
        int (*undo)(void *, MMI32 first, MMI32 end, MMI32 inserted);
        int (*cancel)(void *), (*commit)(void *), (*reserve)(void *, MMI32 entries);
        int (*invalidate)(void *, MMI32 cp, MMI32 span, MMU32 flags);
    } MMTextFormatEditServices;
    /* Paragraph bounds publish before undo setup, including empty ranges. Empty
     * paragraph ranges succeed without undo/dirty effects. Ordinary delta/reserve
     * failures cancel undo; reserve failure also releases generated references.
     * Exceptions retain effects without adding rollback. Success applies, commits,
     * bumps, validates and invalidates (which also marks the document dirty). */
    int mm_text_format_apply(int paragraph, const MMTextFormatConfig *, MMI32 cp, MMI32 span, MMI32 characters,
                             int expand, MMU32 flags, const void *value, const void *old,
                             const MMTextFormatEditServices *);
#ifdef __cplusplus
}
#endif
#endif
