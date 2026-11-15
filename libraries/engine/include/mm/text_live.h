/* Live rich-text save and text-box adoption transactions. */
#ifndef MM_ENGINE_TEXT_LIVE_H
#define MM_ENGINE_TEXT_LIVE_H
#include "text_store.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMTextLiveOutput
    {
        MMTextChunk chunk;
        int number_changed;
    } MMTextLiveOutput;
    enum
    {
        MM_TEXT_ASSERT_MOVE,
        MM_TEXT_ASSERT_LOCATION
    };
    typedef struct MMRichTextLiveServices
    {
        MMRichTextWriteServices write;
        void *context;
        int (*move_text)(void *, MMU32 bytes);
        int (*text)(void *);
        int (*destination)(void *, MMTextLocation *owned);
        int (*redirect)(void *, const MMTextLocation *, MMI32 bytes);
        void (*assertion)(void *, int stage);
        void (*error)(void *);
    } MMRichTextLiveServices;
    /* Borrowed valid live descriptor/stream/groups. Unlike a stored write, a live
     * save publishes RTXT before header validation and retains the created number
     * after child rollback. number_changed lets native callers preserve an untouched,
     * possibly uninitialized number on early failure. TEXT is always written, even
     * empty; the stream's sentinel is excluded by text.bytes. Collection output is
     * native order/system. Failed move diagnoses but continues like AssertDo.
     * Redirect is post-commit: ordinary replace failure is ignored, owned locations
     * always release, and exceptions retain the saved tree. A failed location lookup
     * diagnoses and returns failure without dereferencing a trashed native location.
     * Other write failures remove the partial tree, then report ordinary save errors;
     * negative exception results clean up before the native facade rethrows. */
    int mm_rich_text_save(const MMRichTextState *, MMTextLiveOutput *, int big, int redirect,
                          const MMRichTextLiveServices *);
    typedef struct MMTextBoxStoreServices
    {
        void *context;
        void (*reset_child)(void *);
        int (*save)(void *, MMTextChunk *);
        void (*assert_type)(void *);
        int (*adopt)(void *, MMU32 box, const MMTextChunk *);
        int (*metadata)(void *, MMU32 box);
        void (*remove)(void *, MMU32 box, const MMTextChunk *, int linked);
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
