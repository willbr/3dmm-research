/* Text stream caching, navigation and counted search. */
#ifndef MM_ENGINE_TEXT_ACCESS_H
#define MM_ENGINE_TEXT_ACCESS_H
#include "piece_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TEXT_CACHE_UNITS = 512,
        MM_TEXT_ISOLATED_LF,
        MM_TEXT_NO_FINAL_CR
    };
    enum
    {
        MM_TEXT_CHAR_BREAK = 2,
        MM_TEXT_CHAR_MAY_BREAK = 4,
        MM_TEXT_CHAR_IGNORE = 8
    };
    enum
    {
        MM_TEXT_NAV_IS_START,
        MM_TEXT_NAV_START,
        MM_TEXT_NAV_END,
        MM_TEXT_NAV_PREVIOUS,
        MM_TEXT_NAV_NEXT
    };
    typedef struct MMTextCache
    {
        void *data;
        MMI32 first, end;
        unsigned unit;
    } MMTextCache;
    typedef struct MMTextAccessServices
    {
        void *context;
        const MMTextCache *state;
        int (*cache)(void *, MMI32 first, MMI32 end);
        int (*character)(void *, MMI32 cp, MMU32 *);
        unsigned (*classify)(void *, MMU32);
        int (*lower)(void *, MMU32, MMU32 *);
        int (*lower_pattern)(void *, void *pattern, MMI32 characters);
        void (*diagnostic)(void *, int);
    } MMTextAccessServices;
    /* Borrowed native one/two-byte units, with storage for 512 characters. Ranges
     * are valid native document values. Fill returns MM_DATA_* and publishes the
     * new range on file I/O failure, retaining bytes and poison from the piece reader.
     * Invalidation keeps the longer surviving end; ties retain the front.
     * Synchronous fallible services return 1/0 or -1/-2 and never throw across C.
     * Classification/diagnostics never throw. Get/fetch read the current cache
     * buffer and range after calling the virtual cache service. */
    int mm_text_cache_fill(MMTextCache *, MMI32 first, MMI32 end, MMPieceTable *);
    void mm_text_cache_invalidate(MMTextCache *, MMI32 cp, MMI32 inserted, MMI32 deleted);
    int mm_text_access_character(MMI32 cp, MMU32 *, const MMTextAccessServices *);
    int mm_text_access_fetch(MMI32 cp, MMI32 count, void *output, const MMTextAccessServices *);
    int mm_text_navigate(int kind, MMI32 cp, int word, MMI32 characters, MMI32 *output, const MMTextAccessServices *);
    /* Case-insensitive search lowercases the caller's counted pattern in place,
     * including on miss. It retains the low-byte-only membership skip bitmap for
     * wide text. Found bounds publish together; miss/failure leaves them unchanged. */
    int mm_text_find(void *pattern, MMI32 count, unsigned unit, MMI32 first, MMI32 characters, int sensitive,
                     MMI32 *found_first, MMI32 *found_end, const MMTextAccessServices *);
#ifdef __cplusplus
}
#endif
#endif
