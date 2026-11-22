/* Bytecode emission and postfix compilation for Kauai scripts. */
#ifndef MM_ENGINE_SCRIPT_COMPILE_H
#define MM_ENGINE_SCRIPT_COMPILE_H
#include "script_resource.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMScriptEmitter MMScriptEmitter;

    enum MMScriptCompileError
    {
        MM_SCRIPT_COMPILE_MEMORY,
        MM_SCRIPT_COMPILE_SYNTAX,
        MM_SCRIPT_COMPILE_UNKNOWN_NAME,
        MM_SCRIPT_COMPILE_VARIABLE,
        MM_SCRIPT_COMPILE_DUPLICATE_LABEL,
        MM_SCRIPT_COMPILE_UNDEFINED_LABEL,
        MM_SCRIPT_COMPILE_LABEL_RANGE
    };

    typedef void (*MMScriptCompileDiagnostic)(void *, int error);
    /* The host and diagnostic context are borrowed. Errors discard the code
     * while retaining label diagnostics. The callback may abort the emitter. */
    MMScriptEmitter *mm_script_emitter_create(const MMScriptHost *, unsigned version, unsigned compatible,
                                             MMScriptCompileDiagnostic, void *);
    void mm_script_emitter_abort(MMScriptEmitter *);
    void mm_script_emitter_destroy(MMScriptEmitter *);
    void mm_script_emitter_mark_memory(const MMScriptEmitter *);

    /* Text is counted in the host's native character units and copied. */
    void mm_script_emit_opcode(MMScriptEmitter *, MMU32 opcode);
    void mm_script_emit_immediate(MMScriptEmitter *, MMI32 value);
    void mm_script_emit_variable(MMScriptEmitter *, MMU32 opcode, MMU32 name_high, MMU32 name_low);
    void mm_script_emit_literal(MMScriptEmitter *, const void *text, MMU32 length);
    void mm_script_emit_label(MMScriptEmitter *, const void *name, MMU32 length);
    void mm_script_emit_label_reference(MMScriptEmitter *, const void *name, MMU32 length);
    /* Finish once, then destroy the emitter. The caller owns a successful script. */
    MMCompiledScript *mm_script_emitter_finish(MMScriptEmitter *);

    typedef struct MMScriptToken
    {
        int kind; /* MMLexTokenKind */
        MMI32 value;
        const void *text;
        MMU32 length;
    } MMScriptToken;

    typedef struct MMScriptPostfixInput
    {
        void *context;
        /* Zero marks end of source. Text remains valid until the next token. */
        int (*next)(void *, MMScriptToken *);
        MMU32 (*opcode)(void *, const void *name, MMU32 length); /* Zero on miss. */
    } MMScriptPostfixInput;

    void mm_script_compile_postfix(MMScriptEmitter *, const MMScriptPostfixInput *);
#ifdef __cplusplus
}
#endif
#endif
