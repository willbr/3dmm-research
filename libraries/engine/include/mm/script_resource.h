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
    typedef struct MMScriptHost
    {
        void *context;
        MMBlockTransform decode;
        const MMStorageAllocator *objects;
        const MMStorageAllocator *storage;
        int encoding;
        unsigned unit_bytes;
        MMStringConvert convert;
        void (*invalidate)(void *, void *, size_t);
        void (*mark_memory)(void *allocation);
    } MMScriptHost;

    typedef struct MMCompiledScript MMCompiledScript;
    /* The host is borrowed for the lifetime of the script. The cached reader
     * follows the optional slot-zero GSTX link in the same cache and charges
     * the stored size of the root block. */
    MMCompiledScript *mm_script_create(const MMScriptHost *);
    int mm_script_resource_fetch(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *,
                                 const MMScriptHost *);
    MMCompiledScript *mm_script_resource_archive(const MMChunkAccess *, const MMChunkPayloadServices *, void *archive,
                                                 MMU32 type, MMU32 number, const MMScriptHost *);
    const MMResourceObjectServices *mm_script_object_services(void);
    const MMResourceObject *mm_script_binding(const MMCompiledScript *);
    void mm_script_retain(MMCompiledScript *);
    void mm_script_release(MMCompiledScript *);

    MMU32 mm_script_word_count(const MMCompiledScript *);
    int mm_script_add_word(MMCompiledScript *, MMI32 word);
    void mm_script_set_word(MMCompiledScript *, MMU32 index, MMI32 word);
    MMI32 mm_script_word(const MMCompiledScript *, MMU32 index);
    /* Borrow a span of native instruction words until the script is edited or released. */
    const void *mm_script_word_span(const MMCompiledScript *, MMU32 first, MMU32 count);
    int mm_script_has_literals(const MMCompiledScript *);
    unsigned mm_script_unit_bytes(const MMCompiledScript *);
    MMU32 mm_script_literal_count(const MMCompiledScript *);
    int mm_script_add_literal(MMCompiledScript *, const void *text, MMU32 count, MMU32 *index);
    int mm_script_literal(const MMCompiledScript *, MMU32 index, const void **text, MMU32 *count);
    /* Export native-order legacy GL or GSTX bytes. A missing literal section
     * has size zero, while an empty present section has its usual header. */
    MMU32 mm_script_section_size(const MMCompiledScript *, int literals);
    int mm_script_write_section(MMCompiledScript *, int literals, const MMStorageWriter *);

    typedef struct MMScriptCollection MMScriptCollection;
    /* The owner borrows the host. Size probes report stored bytes. Real loads
     * charge decoded bytes. Registry IDs become native words. Colours remain
     * four-byte records. */
    int mm_script_registry_fetch(MMOwnedBlock *, void **, MMI32 *, const MMScriptHost *);
    int mm_script_colors_fetch(MMOwnedBlock *, void **, MMI32 *, const MMScriptHost *);
    const MMResourceObjectServices *mm_script_collection_services(void);
    void mm_script_collection_release(MMScriptCollection *);
    void mm_script_collection_priority(MMScriptCollection *, MMI32);
    /* Merge in reverse row order, calling put for all readable rows even if a
     * previous call failed. Return zero if a row or insertion failed. */
    typedef int (*MMScriptRegistryPut)(void *, MMI32 id, const void *, MMU32 count);
    int mm_script_registry_merge(MMScriptCollection *, MMScriptRegistryPut, void *context);
    struct MMScriptStrings;
    int mm_script_registry_import(MMScriptCollection *, struct MMScriptStrings *);
    const void *mm_script_color_rows(const MMScriptCollection *, MMU32 *count);
#ifdef __cplusplus
}
#endif
#endif
