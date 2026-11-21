/* Live rich-text save and text-box adoption transactions. */
#ifndef MM_ENGINE_TEXT_LIVE_H
#define MM_ENGINE_TEXT_LIVE_H
#include "text_store.h"
#include "piece_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMTextLiveOutput
    {
        MMTextChunk chunk;
        int number_changed;
    } MMTextLiveOutput;
    struct MMArchiveWriter;
    typedef struct MMRichTextLiveServices
    {
        MMRichTextWriteServices write;
        MMPieceTable *text;
        const struct MMArchiveWriter *archive;
        void *context;
        void (*error)(void *);
    } MMRichTextLiveServices;
    /* Borrowed valid live descriptor/stream/groups. Unlike a stored write, a live
     * save publishes RTXT before header validation and retains the created number
     * after child rollback. number_changed lets native callers preserve an untouched,
     * possibly uninitialized number on early failure. TEXT is always written, even
     * empty; one native text unit is excluded for the live stream's sentinel.
     * Collection output is native order/system. The archive matches the write
     * services' destination. Redirection borrows the owned TEXT file block after
     * the complete tree is written. A failed redirect leaves the save successful.
     * Write failures discard the partial tree and report ordinary save errors;
     * negative exception results clean up before the native facade rethrows. */
    int mm_rich_text_save(const MMRichTextState *, MMTextLiveOutput *, int big, int redirect,
                          const MMRichTextLiveServices *);
    typedef struct MMTextBoxStoreServices
    {
        void *context;
        MMTextBoxState *state;
        const struct MMMovieArchiveEditor *archive;
        int (*save)(void *, MMTextChunk *);
        void (*assert_type)(void *);
    } MMTextBoxStoreServices;
    /* Reset the live child ID before saving. Failed save keeps its own partial
     * effects. Adopt failure deletes the unlinked rich-text root; metadata failure
     * removes its adopted link. No rollback of the reset child ID or written header.
     * Synchronous primitive callbacks follow the text_store 1/0/-1/-2 contract. */
    int mm_text_box_store(MMU32 box, const MMTextBoxStoreServices *);
#ifdef __cplusplus
}
#endif
#endif
