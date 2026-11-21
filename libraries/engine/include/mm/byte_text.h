/* Byte-stream editor cache and line indexing. */
#ifndef MM_ENGINE_BYTE_TEXT_H
#define MM_ENGINE_BYTE_TEXT_H
#include "piece_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum { MM_BYTE_TEXT_CACHE_BYTES = 512 };
    typedef struct MMByteTextView
    {
        MMCollectionStorage storage;
        MMArray lines;
        MMI32 cache_first, cache_end;
        unsigned char cache[MM_BYTE_TEXT_CACHE_BYTES];
    } MMByteTextView;
    /* Owns fixed-width line offsets. Initialize before use, without copying;
     * dispose also accepts zeroed state or a failed initialization. */
    int mm_byte_text_init(MMByteTextView *, const MMStorageAllocator *, int trash_unused);
    int mm_byte_text_dispose(MMByteTextView *);
    /* All positions are byte offsets. Failed file reads remain readable as the
     * piece reader's poison. Fetch/line queries leave the result on range failure.
     * Next-line scanning may fill a bounded output even at an unterminated EOF.
     * A supplied output uses native one- or two-byte character units. */
    int mm_byte_text_fetch(MMByteTextView *, MMPieceTable *, MMI32, unsigned char *);
    int mm_byte_text_line_start(MMByteTextView *, MMPieceTable *, MMI32, MMI32 *);
    int mm_byte_text_next_line(MMByteTextView *, MMPieceTable *, MMI32, MMI32 *, void *, MMI32 capacity, unsigned unit);
    int mm_byte_text_read(MMPieceTable *, MMI32 first, MMI32 count, void *, unsigned unit);
    void mm_byte_text_invalidate(MMByteTextView *, MMI32 first, MMI32 inserted, MMI32 deleted);
    MMI32 mm_byte_text_line_at(const MMByteTextView *, MMI32 line, MMI32 end);
    MMI32 mm_byte_text_line_index(const MMByteTextView *, MMI32 at, MMI32 visible);
    void mm_byte_text_set_first_line(MMByteTextView *, MMI32 at);
    /* An allocation failure can leave a valid partial index. EOF truncates the
     * index. Edits report the line changes made before a partial rebuild ends. */
    int mm_byte_text_reformat(MMByteTextView *, MMPieceTable *, MMI32 first_line, MMI32 visible);
    int mm_byte_text_reformat_edit(MMByteTextView *, MMPieceTable *, MMI32 visible, MMI32 first, MMI32 inserted,
                                  MMI32 deleted, MMI32 *changed_line, MMI32 *added, MMI32 *removed);
    typedef struct MMByteTextEditServices
    {
        void *context;
        MMPieceTable *(*pieces)(void *, void *document);
        int (*hide)(void *);
        int (*invalidate)(void *, MMI32 at, MMI32 inserted, MMI32 deleted);
        int (*caret)(void *, MMI32 at);
        int (*show)(void *);
    } MMByteTextEditServices;
    /* The view supplies its current piece table for a null document. Effects
     * may return negative errors, without throwing across C. A failed splice
     * leaves the selection hidden and skips invalidation and caret movement. */
    int mm_byte_text_edit(const void *, MMI32 bytes, MMI32 anchor, MMI32 focus, const MMByteTextEditServices *);
    int mm_byte_text_finish_edit(MMI32 at, MMI32 inserted, MMI32 deleted, const MMByteTextEditServices *);
    MMI32 mm_byte_text_rebase(MMI32 position, MMI32 at, MMI32 inserted, MMI32 deleted);
#ifdef __cplusplus
}
#endif
#endif
