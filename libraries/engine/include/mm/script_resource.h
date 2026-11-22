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
        void (*release)(void *, void *);
    } MMScriptResourceServices;
    /* Readers return owned collections with borrowed views. Script construction
     * adopts its inputs only on success. All calls are synchronous.
     * A cached script reads the supplied root block and follows its optional
     * slot-zero GSTX link in the same cache. Its cache charge uses stored bytes. */
    int mm_script_resource_fetch(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *,
                                 const MMScriptResourceServices *);
    void *mm_script_resource_archive(const MMChunkAccess *, const MMChunkPayloadServices *, void *archive,
                                     MMU32 type, MMU32 number, const MMScriptResourceServices *);
    typedef struct MMScriptCollection MMScriptCollection;
    typedef struct MMScriptCollectionHost
    {
        void *context;
        MMBlockTransform decode;
        const MMStorageAllocator *objects;
        const MMStorageAllocator *storage;
        int encoding;
        unsigned unit_bytes;
        MMStringConvert convert;
        void (*invalidate)(void *, void *, size_t);
        void (*mark)(void *owner, const MMCollectionStorage *, const MMResourceObject *);
    } MMScriptCollectionHost;
    /* The owner borrows the host. Size probes report stored bytes. Real loads
     * charge decoded bytes. Registry IDs become native words. Colours remain
     * four-byte records. */
    int mm_script_registry_fetch(MMOwnedBlock *, void **, MMI32 *, const MMScriptCollectionHost *);
    int mm_script_colors_fetch(MMOwnedBlock *, void **, MMI32 *, const MMScriptCollectionHost *);
    const MMResourceObjectServices *mm_script_collection_services(void);
    void mm_script_collection_release(MMScriptCollection *);
    void mm_script_collection_priority(MMScriptCollection *, MMI32);
    /* Merge in reverse row order, calling put for all readable rows even if a
     * previous call failed. Return zero if a row or insertion failed. */
    typedef int (*MMScriptRegistryPut)(void *, MMI32 id, const void *, MMU32 count);
    int mm_script_registry_merge(MMScriptCollection *, MMScriptRegistryPut, void *context);
    const void *mm_script_color_rows(const MMScriptCollection *, MMU32 *count);
#ifdef __cplusplus
}
#endif
#endif
