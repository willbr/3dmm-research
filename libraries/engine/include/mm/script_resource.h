/* Script bytecode, string registry and colour table loading. */
#ifndef MM_ENGINE_SCRIPT_RESOURCE_H
#define MM_ENGINE_SCRIPT_RESOURCE_H
#include "chunk_storage.h"
#include "resource_cache.h"
#include "string_table.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMScriptResourceServices
    {
        void *context;
        MMBlockTransform decode;
        void *(*read_rows)(void *, const MMReadStream *, MMArray *, int *other_order);
        void *(*read_strings)(void *, const MMReadStream *, MMStringTable *, int *other_order);
        void *(*create_script)(void *, void *words, void *literals);
        void *(*wrap_collection)(void *, void *);
        void (*release)(void *, void *);
        void (*invalidate)(void *, void *, size_t);
    } MMScriptResourceServices;
    /* Readers return owned collections with borrowed views. Script construction
     * and wrapping adopt their inputs only on success. All calls are synchronous.
     * A cached script reads the supplied root block and follows its optional
     * slot-zero GSTX link in the same cache. Its cache charge uses stored bytes. */
    int mm_script_resource_fetch(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *,
                                 const MMScriptResourceServices *);
    void *mm_script_resource_archive(const MMChunkAccess *, const MMChunkPayloadServices *, void *archive,
                                     MMU32 type, MMU32 number, const MMScriptResourceServices *);
    /* Size probes report stored bytes. Real loads charge decoded bytes. The
     * registry normalizes the four-byte IDs. Colour rows remain byte records. */
    int mm_script_registry_fetch(MMOwnedBlock *, void **, MMI32 *, const MMScriptResourceServices *);
    int mm_script_colors_fetch(MMOwnedBlock *, void **, MMI32 *, const MMScriptResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
