#ifndef MM_ENGINE_SCRIPT_COMPILE_INTERNAL_H
#define MM_ENGINE_SCRIPT_COMPILE_INTERNAL_H
#include "mm/script_compile.h"

const MMScriptHost *mm_script_emitter_host(MMScriptEmitter *);
void mm_script_emitter_error(MMScriptEmitter *, int error);

typedef struct MMScriptFunction
{
    MMU32 opcode;
    MMI32 fixed, group, minimum_groups;
    int returns_void;
} MMScriptFunction;

int mm_script_name_matches(const char *ascii, const void *text, MMU32 length, unsigned unit);
MMU32 mm_script_find_opcode(int dialect, const void *name, MMU32 length, unsigned unit);
const MMScriptFunction *mm_script_find_function(int dialect, const void *name, MMU32 length, unsigned unit);
const char *mm_script_opcode_name(int dialect, MMU32 opcode);

#endif
