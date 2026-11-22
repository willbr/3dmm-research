#ifndef MM_ENGINE_SCRIPT_COMPILE_INTERNAL_H
#define MM_ENGINE_SCRIPT_COMPILE_INTERNAL_H
#include "mm/script_compile.h"

const MMScriptHost *mm_script_emitter_host(MMScriptEmitter *);
void mm_script_emitter_error(MMScriptEmitter *, int error);

#endif
