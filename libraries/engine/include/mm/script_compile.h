/* Bytecode emission and compilation for Kauai scripts. */
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
        MM_SCRIPT_COMPILE_LABEL_RANGE,
        MM_SCRIPT_COMPILE_DIVIDE_BY_ZERO,
        MM_SCRIPT_COMPILE_DIVISION_OVERFLOW,
        MM_SCRIPT_COMPILE_UNKNOWN_FUNCTION,
        MM_SCRIPT_COMPILE_ARGUMENT_COUNT,
        MM_SCRIPT_COMPILE_VOID_VALUE,
        MM_SCRIPT_COMPILE_KEYWORD_VARIABLE,
        MM_SCRIPT_COMPILE_UNEXPECTED_ELIF,
        MM_SCRIPT_COMPILE_UNEXPECTED_ELSE,
        MM_SCRIPT_COMPILE_UNEXPECTED_END,
        MM_SCRIPT_COMPILE_UNEXPECTED_CONTINUE,
        MM_SCRIPT_COMPILE_UNEXPECTED_BREAK,
        MM_SCRIPT_COMPILE_UNEXPECTED_EOF
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

    enum MMScriptDialect
    {
        MM_SCRIPT_BASE,
        MM_SCRIPT_KIDSPACE
    };

    typedef struct MMScriptInput
    {
        void *context;
        /* Zero marks end of source. Text remains valid until the next token. */
        int (*next)(void *, MMScriptToken *);
        int dialect;
    } MMScriptInput;

    void mm_script_compile_postfix(MMScriptEmitter *, const MMScriptInput *);
    void mm_script_compile_infix(MMScriptEmitter *, const MMScriptInput *);

    enum MMScriptDisassembleResult
    {
        MM_SCRIPT_DISASSEMBLED,
        MM_SCRIPT_DISASSEMBLE_NO_VERSION,
        MM_SCRIPT_DISASSEMBLE_VERSION,
        MM_SCRIPT_DISASSEMBLE_INSTRUCTION,
        MM_SCRIPT_DISASSEMBLE_VARIABLE_INSTRUCTION,
        MM_SCRIPT_DISASSEMBLE_VARIABLE,
        MM_SCRIPT_DISASSEMBLE_LITERAL,
        MM_SCRIPT_DISASSEMBLE_VARIABLE_OPCODE,
        MM_SCRIPT_DISASSEMBLE_OPCODE
    };

    /* Each fragment uses the script's character width and is valid for the call.
     * The sink adds a line break when end_line is set. The emitted prefix is
     * retained by the sink when a malformed instruction interrupts the listing. */
    typedef void (*MMScriptTextWrite)(void *, const void *text, MMU32 length, int end_line);
    int mm_script_disassemble(const MMCompiledScript *, int dialect, unsigned current, unsigned minimum,
                              MMScriptTextWrite, void *);
#ifdef __cplusplus
}
#endif
#endif
