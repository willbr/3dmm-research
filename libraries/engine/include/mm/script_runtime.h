/* Script operand stack and runtime variable scopes. */
#ifndef MM_ENGINE_SCRIPT_RUNTIME_H
#define MM_ENGINE_SCRIPT_RUNTIME_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Initialize before use. Disposal clears the contents and retains the
     * allocator so the same owner can be used for another script. */
    typedef struct MMScriptStack
    {
        MMCollectionStorage storage;
        MMU32 count;
        int trash_unused;
    } MMScriptStack;

    int mm_script_stack_init(MMScriptStack *, const MMStorageAllocator *, int trash_unused);
    void mm_script_stack_dispose(MMScriptStack *);
    int mm_script_stack_reserve(MMScriptStack *, MMU32 additional);
    /* Copy native 32-bit words from storage that does not alias the stack. */
    int mm_script_stack_push(MMScriptStack *, const void *words, MMU32 count);
    int mm_script_stack_pop(MMScriptStack *, MMI32 *word);
    /* Borrow the top count words, oldest first, until the next stack mutation. */
    const void *mm_script_stack_top(const MMScriptStack *, MMI32 count);
    int mm_script_stack_drop(MMScriptStack *, MMI32 count);
    int mm_script_stack_duplicate(MMScriptStack *, MMI32 count);
    int mm_script_stack_reverse(MMScriptStack *, MMI32 count);
    int mm_script_stack_rotate(MMScriptStack *, MMI32 count, MMI32 left);
    int mm_script_stack_select(MMScriptStack *, MMI32 count, MMI32 from_top);
    /* Scan (value, key) pairs from the top and replace them with the first
     * matching value, or fallback. The caller has already popped key and fallback. */
    int mm_script_stack_match(MMScriptStack *, MMI32 pairs, MMI32 key, MMI32 fallback);

    typedef struct MMScriptVariables
    {
        MMCollectionStorage storage;
        MMU32 count;
    } MMScriptVariables;

    int mm_script_variables_init(MMScriptVariables *, const MMStorageAllocator *);
    void mm_script_variables_dispose(MMScriptVariables *);
    /* Return whether the key was found. On a miss, leave the output alone. */
    int mm_script_variables_get(const MMScriptVariables *, MMU32 high, MMU32 low, MMI32 *word);
    int mm_script_variables_set(MMScriptVariables *, MMU32 high, MMU32 low, MMI32 word);
    /* Pack the first eight native character units into a 48-bit name. Invalid
     * units encode as underscores and make the function return zero. */
    int mm_script_variable_name(const void *text, MMU32 count, unsigned unit_bytes, MMU32 *high, MMU32 *low);
    enum { MM_SCRIPT_VARIABLE_TEXT_CAPACITY = 16 };
    /* Write the ASCII name and optional [index], then a terminator. */
    MMU32 mm_script_variable_text(MMU32 high, MMU32 low, char text[MM_SCRIPT_VARIABLE_TEXT_CAPACITY]);
    int mm_script_version_readable(MMU32 version, unsigned current, unsigned minimum);
#ifdef __cplusplus
}
#endif
#endif
