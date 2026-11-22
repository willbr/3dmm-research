/* Rich-text property maps, edit boundaries and shared argument references. */
#ifndef MM_ENGINE_TEXT_PROPERTIES_H
#define MM_ENGINE_TEXT_PROPERTIES_H
#include "collections.h"
#include "text_document.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_TEXT_ARG_INDEX,
        MM_TEXT_ARG_REFS,
        MM_TEXT_ARG_OVERFLOW,
        MM_TEXT_PROP_INSERT,
        MM_TEXT_ARG_MUTATION
    };
    typedef struct MMTextPropertyEntry
    {
        MMU32 key;
        MMI32 value;
    } MMTextPropertyEntry;
    typedef struct MMTextPropertyChange
    {
        unsigned kind;
        MMU32 value, mask;
    } MMTextPropertyChange;
    typedef struct MMTextArgumentRows
    {
        void *context;
        MMI32 (*count)(void *, void *);
        int (*free_slot)(void *, void *, MMI32);
        const void *(*data)(void *, void *, MMI32, MMI32 *bytes);
        MMU32 (*fixed)(void *, void *, MMI32);
        int (*put_fixed)(void *, void *, MMI32, MMU32);
        int (*add)(void *, void *, const void *, MMI32 bytes, MMU32 fixed, MMI32 *index);
        int (*remove)(void *, void *, MMI32);
    } MMTextArgumentRows;
    /* Bind argument operations for C collections. References follow the
     * collection's payload byte order. */
    void mm_text_collection_argument_rows(MMTextArgumentRows *);
    /* Borrow the variable payload of a live row until mutation. A missing row
     * returns NULL and zero size. The caller controls the storage lock. */
    unsigned char *mm_text_argument_payload(const MMGroup *, MMI32 index, MMI32 *bytes);
    typedef struct MMTextArgumentServices
    {
        void *context;
        MMTextDocumentObjects objects;
        int (*create)(void *, MMI32 bytes, void **owned);
        MMTextArgumentRows rows;
        void (*diagnostic)(void *, int);
        void (*validate_input)(void *, unsigned kind, const void *, MMI32 bytes);
    } MMTextArgumentServices;
    typedef struct MMTextPropertyRows
    {
        void *context;
        MMI32 (*count)(void *);
        void (*get)(void *, MMI32 index, MMTextPropertyEntry *);
        void (*put)(void *, MMI32 index, const MMTextPropertyEntry *);
        int (*insert)(void *, MMI32 index, const MMTextPropertyEntry *);
        void (*remove)(void *, MMI32 index);
    } MMTextPropertyRows;
    typedef struct MMTextPropertyBinding
    {
        MMTextCollection *collection;
        void *context;
        void (*bad_mutation)(void *);
        int insert_failure;
    } MMTextPropertyBinding;
    /* Borrow the collection and binding for a synchronous edit. */
    void mm_text_collection_property_rows(MMTextPropertyRows *, const MMTextPropertyBinding *);
    int mm_text_collection_reserve_properties(MMTextCollection *, MMI32 count);
    int mm_text_property_array_read(const MMArray *, MMU32 row, int big, MMU32 words[2]);
    int mm_text_property_array_write(MMArray *, MMU32 row, int big, const MMU32 words[2], int insert);
    typedef struct MMTextPropertyServices
    {
        void *context;
        MMTextArgumentServices arguments;
        MMTextPropertyRows rows;
        void (*clear_cache)(void *);
        void (*change)(void *, MMI32 index, MMTextPropertyChange *);
        int (*paragraph_end)(void *, MMI32 cp, MMI32 *end);
        void (*diagnostic)(void *, int);
    } MMTextPropertyServices;
    /* Sorted unique keys pack an eight-bit kind and a 24-bit character position.
     * Find publishes insertion/predecessor index and range end on either result;
     * its entry is unchanged on miss. Fetch leaves all outputs unchanged on miss.
     * Optional outputs may be null. Inputs/ranges are valid native document values. */
    int mm_text_property_find(MMU32 key, MMI32 characters, MMTextPropertyEntry *, MMI32 *end, MMI32 *index,
                              const MMTextPropertyServices *);
    int mm_text_property_fetch(MMI32 index, MMI32 characters, MMTextPropertyEntry *, MMI32 *end,
                               const MMTextPropertyServices *);
    int mm_text_property_has_argument(unsigned kind);
    /* New groups publish immediately, including a partial owned failure output.
     * Ensure scans backward, skipping holes and comparing bytes before kind; it
     * returns a one-based reference. Failed ensure leaves its output unchanged.
     * Reference operations diagnose invalid indices without changing the group.
     * Built-in kinds retain the legacy font-only exception to the client even rule. */
    int mm_text_argument_ensure(unsigned kind, const void *, MMI32 bytes, MMI32 *reference,
                                const MMTextArgumentServices *);
    void mm_text_argument_ref(MMI32 reference, int add, const MMTextArgumentServices *);
    void mm_text_property_ref(unsigned kind, MMI32 value, int add, const MMTextArgumentServices *);
    /* Text has already changed before adjust. Paragraph ends are queried in that
     * new text. Apply consumes one reference per change, in reverse input order.
     * The caller reserves room for twice the change count; ordinary insert failure
     * diagnoses and continues as the legacy AssertDo did. Negative exception results
     * stop with partial effects retained. Neither operation rolls back edits/references.
     * Synchronous fallible services return 1/0 or -1/-2 and never throw across C;
     * collection access, diagnostics, publication and cache clearing never throw. */
    int mm_text_properties_adjust(MMI32 cp, MMI32 inserted, MMI32 deleted, MMI32 characters,
                                  const MMTextPropertyServices *);
    int mm_text_properties_apply(MMI32 cp, MMI32 span, MMI32 characters, MMI32 count, const MMTextPropertyServices *);
    int mm_text_properties_apply_changes(MMI32 cp, MMI32 span, MMI32 characters,
                                         const MMTextPropertyChange *, MMI32 count, const MMTextPropertyServices *);
    void mm_text_properties_release(MMI32 count, const MMTextPropertyServices *);
#ifdef __cplusplus
}
#endif
#endif
