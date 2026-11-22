/* Execution state and host services for compiled Kauai scripts. */
#ifndef MM_ENGINE_SCRIPT_VM_H
#define MM_ENGINE_SCRIPT_VM_H
#include "script_resource.h"
#include "script_runtime.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum MMScriptScope
    {
        MM_SCRIPT_SCOPE_THIS = 1,
        MM_SCRIPT_SCOPE_GLOBAL,
        MM_SCRIPT_SCOPE_REMOTE
    };

    enum MMScriptError
    {
        MM_SCRIPT_ERROR_NONE,
        MM_SCRIPT_ERROR_STATE,
        MM_SCRIPT_ERROR_VERSION,
        MM_SCRIPT_ERROR_INSTRUCTION,
        MM_SCRIPT_ERROR_VARIABLE,
        MM_SCRIPT_ERROR_OPERAND,
        MM_SCRIPT_ERROR_OPCODE,
        MM_SCRIPT_ERROR_RESOURCE
    };

    typedef struct MMScriptVMServices
    {
        MMScriptVariables *(*variables)(void *, int scope, MMI32 object);
        void (*missing_variable)(void *, MMU32 high, MMU32 low);
        void (*error)(void *, int reason);
        MMI32 (*random)(void *, MMI32 limit);
        /* NULL words shuffles the range [0, count). Otherwise copy count words. */
        void (*shuffle)(void *, MMI32 count, const void *words);
        MMI32 (*next_card)(void *);
        int (*opcode)(void *, unsigned opcode);
    } MMScriptVMServices;

    typedef struct MMScriptVM
    {
        MMScriptStack stack;
        MMScriptVariables locals;
        MMCompiledScript *script;
        const MMScriptVMServices *services;
        void *host;
        MMU32 pc, word_count;
        MMI32 result;
        int paused, error;
    } MMScriptVM;

    /* Services and context are borrowed. Callbacks may use the operand helpers
     * and pause execution, and may run a separate VM. */
    int mm_script_vm_init(MMScriptVM *, const MMStorageAllocator *, int trash_unused,
                          const MMScriptVMServices *, void *host);
    /* Dispose releases the script, operands and locals. The result and error
     * are available until the next attach. The owner can be reused. */
    void mm_script_vm_dispose(MMScriptVM *);
    /* Attach to a detached VM and retain the script. Dispose after a failed
     * attach or after running to completion. Return nonzero on success. */
    int mm_script_vm_attach(MMScriptVM *, MMCompiledScript *, unsigned current, unsigned minimum);
    int mm_script_vm_resume(MMScriptVM *);

    void mm_script_vm_fail(MMScriptVM *, int reason);
    void mm_script_vm_push(MMScriptVM *, MMI32 word);
    MMI32 mm_script_vm_pop(MMScriptVM *);
    const void *mm_script_vm_words(MMScriptVM *, MMI32 count);
    void mm_script_vm_drop(MMScriptVM *, MMI32 count);
#ifdef __cplusplus
}
#endif
#endif
