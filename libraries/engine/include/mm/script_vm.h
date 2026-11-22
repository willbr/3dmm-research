/* Execution state and host services for compiled Kauai scripts. */
#ifndef MM_ENGINE_SCRIPT_VM_H
#define MM_ENGINE_SCRIPT_VM_H
#include "random.h"
#include "script_resource.h"
#include "script_runtime.h"
#include "script_strings.h"
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

    enum MMScriptStringNotice
    {
        MM_SCRIPT_STRING_MISSING,
        MM_SCRIPT_STRING_STORE_FAILED,
        MM_SCRIPT_STRING_RESOURCE_MISSING,
        MM_SCRIPT_STRING_MERGE_FAILED,
        MM_SCRIPT_STRING_BAD_SLICE,
        MM_SCRIPT_STRING_NO_REGISTRY
    };

    typedef struct MMScriptVMServices
    {
        MMScriptVariables *(*variables)(void *, int scope, MMI32 object);
        void (*missing_variable)(void *, MMU32 high, MMU32 low);
        void (*error)(void *, int reason);
        /* Interpreters in one host can share draws and a deck. */
        MMRandom *random;
        MMShuffle *deck;
        void (*scale_time)(void *, MMU32 scale);
        /* Return a retained registry resource. The VM releases the reference. */
        MMScriptCollection *(*string_resource)(void *, MMU32 number, MMU32 scope);
        void (*string_notice)(void *, int reason, MMI32 value);
    } MMScriptVMServices;

    struct MMKidspaceScriptServices;

    typedef struct MMScriptVM
    {
        MMScriptStack stack;
        MMScriptVariables locals;
        MMCompiledScript *script;
        MMScriptStrings *strings;
        const MMScriptVMServices *services;
        void *host;
        /* Optional Kidspace operations use the same host context. */
        const struct MMKidspaceScriptServices *kidspace;
        MMI32 this_object;
        MMU32 pc, word_count;
        MMI32 result;
        int paused, error;
    } MMScriptVM;

    /* Services, context and an optional shared registry are borrowed.
     * Callbacks can run other VMs but must not attach, resume or dispose this VM. */
    int mm_script_vm_init(MMScriptVM *, const MMStorageAllocator *, int trash_unused,
                          MMScriptStrings *, const MMScriptVMServices *, void *host);
    /* Dispose removes the script's literal IDs and releases the script,
     * operands and locals. Result and error survive until the next attach. */
    void mm_script_vm_dispose(MMScriptVM *);
    /* Attach to a detached VM, retaining the script and installing parameters
     * (native 32-bit words), then literals. Dispose after a failed attach or
     * after running to completion. Return nonzero on success. */
    int mm_script_vm_attach(MMScriptVM *, MMCompiledScript *, unsigned current, unsigned minimum,
                            const void *parameters, MMU32 parameter_count);
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
