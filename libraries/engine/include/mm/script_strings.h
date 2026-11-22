/* Runtime strings shared by script interpreters and Help. */
#ifndef MM_ENGINE_SCRIPT_STRINGS_H
#define MM_ENGINE_SCRIPT_STRINGS_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMScriptStrings
    {
        MMCollectionStorage storage;
        MMU32 count, bytes, last_id;
        unsigned unit_bytes;
        int trash_unused;
    } MMScriptStrings;

    int mm_script_strings_init(MMScriptStrings *, unsigned unit_bytes, int trash_unused,
                               const MMStorageAllocator *);
    void mm_script_strings_dispose(MMScriptStrings *);
    /* Borrow text until the next mutation. A miss publishes NULL and zero. */
    int mm_script_strings_get(const MMScriptStrings *, MMI32 id, const void **text, MMU32 *count);
    /* Input text must be independent of registry storage. */
    int mm_script_strings_put(MMScriptStrings *, MMI32 id, const void *text, MMU32 count);
    int mm_script_strings_add(MMScriptStrings *, MMI32 *id, const void *text, MMU32 count);
    void mm_script_strings_delete(MMScriptStrings *, MMI32 id);
    int mm_script_strings_move(MMScriptStrings *, MMI32 source, MMI32 destination);
#ifdef __cplusplus
}
#endif
#endif
