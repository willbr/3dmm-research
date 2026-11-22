/* Compilation converted from Kauai scrcom.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "script_compile_internal.h"
#include "mm/lex_token.h"
#include "mm/script_opcodes.h"
#include "mm/script_runtime.h"
#include "mm/string_ops.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

typedef struct LabelTable
{
    MMCollectionStorage storage;
    MMStringTable strings;
} LabelTable;

struct MMScriptEmitter
{
    const MMScriptHost *host;
    MMCompiledScript *script;
    MMScriptCompileDiagnostic diagnostic;
    void *context;
    LabelTable labels, references;
    MMStringOps names;
    MMU32 last_opcode;
    int force_opcode;
};

static int reserve_labels(void *context, MMStringTable *table, MMU32 bytes, MMU32 entries)
{
    MMCollectionStorage *storage = context;
    int status = mm_storage_ensure(storage, bytes, entries, 0);
    table->strings = storage->buffers[0].data;
    table->entries = storage->buffers[1].data;
    table->string_capacity = storage->buffers[0].capacity;
    table->entry_capacity = storage->buffers[1].capacity;
    return status == MM_STORAGE_OK;
}

static int init_labels(LabelTable *table, const MMScriptHost *host, MMU32 entries, MMU32 units)
{
    if (mm_storage_init(&table->storage, host->storage) != MM_STORAGE_OK)
        return 0;
    table->strings.stride = 8;
    table->strings.free_count = -1;
    table->strings.unit_bytes = host->unit_bytes;
    table->strings.context = &table->storage;
    table->strings.reserve = reserve_labels;
    table->storage.min_grow[0] = units * host->unit_bytes;
    table->storage.min_grow[1] = entries * table->strings.stride;
    return 1;
}

MMScriptEmitter *mm_script_emitter_create(const MMScriptHost *host, unsigned version, unsigned compatible,
                                         MMScriptCompileDiagnostic diagnostic, void *context)
{
    const MMStorageAllocator *allocator;
    MMScriptEmitter *emitter;
    if (!host || (host->unit_bytes != 1 && host->unit_bytes != 2) || version > 0xffff || compatible > 0xffff)
        return NULL;
    allocator = host->objects;
    if (allocator && (!allocator->resize || !allocator->release))
        return NULL;
    emitter = allocator ? allocator->resize(allocator->context, NULL, 0, sizeof(*emitter), MM_STORAGE_NORMAL)
                        : malloc(sizeof(*emitter));
    if (!emitter)
        return NULL;

    memset(emitter, 0, sizeof(*emitter));
    emitter->host = host;
    emitter->diagnostic = diagnostic;
    emitter->context = context;
    emitter->names.unit = host->unit_bytes;
    emitter->last_opcode = 1;
    emitter->force_opcode = 1;
    if (!init_labels(&emitter->labels, host, 5, 100) || !init_labels(&emitter->references, host, 10, 200) ||
        !(emitter->script = mm_script_create(host)) ||
        !mm_script_add_word(emitter->script, mm_signed_word(((MMU32)version << 16) | compatible)))
    {
        mm_script_emitter_destroy(emitter);
        return NULL;
    }
    return emitter;
}

void mm_script_emitter_abort(MMScriptEmitter *emitter)
{
    if (emitter)
    {
        mm_script_release(emitter->script);
        emitter->script = NULL;
    }
}

void mm_script_emitter_destroy(MMScriptEmitter *emitter)
{
    const MMStorageAllocator *allocator;
    if (!emitter)
        return;
    allocator = emitter->host->objects;
    mm_script_emitter_abort(emitter);
    mm_storage_dispose(&emitter->labels.storage);
    mm_storage_dispose(&emitter->references.storage);
    if (allocator)
        allocator->release(allocator->context, emitter, sizeof(*emitter));
    else
        free(emitter);
}

void mm_script_emitter_mark_memory(const MMScriptEmitter *emitter)
{
    void (*mark)(void *);
    if (!emitter || !emitter->host->mark_memory)
        return;
    mark = emitter->host->mark_memory;
    mark((void *)emitter);
    mark(emitter->labels.strings.strings);
    mark(emitter->labels.strings.entries);
    mark(emitter->references.strings.strings);
    mark(emitter->references.strings.entries);
    if (emitter->script)
        mm_script_object_services()->mark_memory(emitter->script);
}

const MMScriptHost *mm_script_emitter_host(MMScriptEmitter *emitter)
{
    return emitter->host;
}

void mm_script_emitter_error(MMScriptEmitter *emitter, int error)
{
    mm_script_emitter_abort(emitter);
    if (emitter->diagnostic)
        emitter->diagnostic(emitter->context, error);
}

static void end_opcode(MMScriptEmitter *emitter)
{
    MMU32 count = mm_script_word_count(emitter->script);
    MMU32 index = emitter->last_opcode;
    MMU32 word;
    if (index < count - 1)
    {
        word = (MMU32)mm_script_word(emitter->script, index);
        word = (word & 0xff00ffffUL) | ((count - index - 1) << 16);
        mm_script_set_word(emitter->script, index, mm_signed_word(word));
    }
}

void mm_script_emit_opcode(MMScriptEmitter *emitter, MMU32 opcode)
{
    if (!emitter || !emitter->script)
        return;
    if (opcode > 0xffff)
    {
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_SYNTAX);
        return;
    }
    end_opcode(emitter);
    emitter->last_opcode = mm_script_word_count(emitter->script);
    if (!mm_script_add_word(emitter->script, (MMI32)opcode))
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_MEMORY);
    emitter->force_opcode = 0;
}

void mm_script_emit_immediate(MMScriptEmitter *emitter, MMI32 value)
{
    if (!emitter || !emitter->script)
        return;
    if (emitter->force_opcode || mm_script_word_count(emitter->script) - emitter->last_opcode >= 256)
        mm_script_emit_opcode(emitter, opNil);
    if (emitter->script && !mm_script_add_word(emitter->script, value))
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_MEMORY);
}

void mm_script_emit_variable(MMScriptEmitter *emitter, MMU32 opcode, MMU32 high, MMU32 low)
{
    if (!emitter || !emitter->script)
        return;
    if (!opcode || opcode > 255 || high > 0xffff)
    {
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_VARIABLE);
        return;
    }
    mm_script_emit_opcode(emitter, opNil);
    if (emitter->script)
    {
        mm_script_set_word(emitter->script, emitter->last_opcode, mm_signed_word((opcode << 24) | high));
        if (!mm_script_add_word(emitter->script, mm_signed_word(low)))
            mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_MEMORY);
    }
}

void mm_script_emit_literal(MMScriptEmitter *emitter, const void *text, MMU32 length)
{
    MMU32 index;
    if (!emitter || !emitter->script)
        return;
    if (!mm_script_add_literal(emitter->script, text, length, &index))
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_MEMORY);
    else
        mm_script_emit_variable(emitter, kopPushLocVar, 0, index);
}

static int compare_name(void *context, const void *left, MMU32 left_count, const void *right, MMU32 right_count)
{
    MMU32 value = mm_string_internal_compare(context, left, left_count, right, right_count);
    return value == MM_STRING_LESS ? -1 : value == MM_STRING_GREATER ? 1 : 0;
}

void mm_script_emit_label(MMScriptEmitter *emitter, const void *name, MMU32 length)
{
    MMU32 index, target;
    if (!emitter)
        return;
    index = emitter->labels.strings.count;
    if (mm_strings_find(&emitter->labels.strings, name, length, compare_name, &emitter->names, &index))
    {
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_DUPLICATE_LABEL);
        return;
    }
    target = emitter->script ? mm_script_word_count(emitter->script) : 0;
    if (target > 0x00ffffffUL)
    {
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_LABEL_RANGE);
        return;
    }
    target |= (MMU32)MM_SCRIPT_LABEL_BYTE << 24;
    emitter->force_opcode = 1;
    if (!mm_strings_insert(&emitter->labels.strings, index, name, length, &target))
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_MEMORY);
}

void mm_script_emit_label_reference(MMScriptEmitter *emitter, const void *name, MMU32 length)
{
    MMU32 address;
    if (!emitter || !emitter->script)
        return;
    mm_script_emit_immediate(emitter, 0);
    if (!emitter->script)
        return;
    address = mm_script_word_count(emitter->script) - 1;
    if (!mm_strings_add(&emitter->references.strings, name, length, &address, NULL))
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_MEMORY);
}

MMCompiledScript *mm_script_emitter_finish(MMScriptEmitter *emitter)
{
    MMCompiledScript *script;
    MMStringTable *refs, *labels;
    MMU32 i, index, length, address, target;
    const void *name;
    if (!emitter)
        return NULL;
    if (emitter->script)
        end_opcode(emitter);
    refs = &emitter->references.strings;
    labels = &emitter->labels.strings;
    for (i = refs->count; i-- > 0;)
    {
        if (!mm_strings_get(refs, i, &name, &length) ||
            !mm_strings_find(labels, name, length, compare_name, &emitter->names, &index))
            mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_UNDEFINED_LABEL);
        else if (emitter->script)
        {
            memcpy(&address, refs->entries + i * refs->stride + 4, 4);
            memcpy(&target, labels->entries + index * labels->stride + 4, 4);
            mm_script_set_word(emitter->script, address, mm_signed_word(target));
        }
    }
    script = emitter->script;
    emitter->script = NULL;
    return script;
}

static void postfix_variable(MMScriptEmitter *emitter, const MMScriptPostfixInput *input, int pop)
{
    MMScriptToken token;
    MMU32 opcode, high, low;
    if (!input->next(input->context, &token))
    {
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_SYNTAX);
        return;
    }
    opcode = kopPushLocVar;
    switch (token.kind)
    {
    case ttBNot: opcode = kopPushRemoteVar; break;
    case ttDot: opcode = kopPushThisVar; break;
    case ttScope: opcode = kopPushGlobalVar; break;
    }
    if (opcode != kopPushLocVar && !input->next(input->context, &token))
    {
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_SYNTAX);
        return;
    }
    if (token.kind == ttBAnd)
    {
        opcode += kopPushLocArray - kopPushLocVar;
        if (!input->next(input->context, &token))
        {
            mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_SYNTAX);
            return;
        }
    }
    if (token.kind != ttName || input->opcode(input->context, token.text, token.length) ||
        !mm_script_variable_name(token.text, token.length, emitter->host->unit_bytes, &high, &low))
        mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_VARIABLE);
    else
        mm_script_emit_variable(emitter, opcode + pop, high, low);
}

void mm_script_compile_postfix(MMScriptEmitter *emitter, const MMScriptPostfixInput *input)
{
    MMScriptToken token;
    MMU32 opcode;
    int kind;
    while (input->next(input->context, &token))
    {
        if (!emitter)
            continue;
        switch (token.kind)
        {
        case ttLong:
            mm_script_emit_immediate(emitter, token.value);
            break;
        case ttString:
            mm_script_emit_literal(emitter, token.text, token.length);
            break;
        case ttSub:
            if (input->next(input->context, &token) && token.kind == ttLong)
                mm_script_emit_immediate(emitter, mm_signed_word((MMU32)0 - (MMU32)token.value));
            else
                mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_SYNTAX);
            break;
        case ttName:
            opcode = input->opcode(input->context, token.text, token.length);
            if (opcode)
                mm_script_emit_opcode(emitter, opcode);
            else
                mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_UNKNOWN_NAME);
            break;
        case ttGt: case ttLt:
            postfix_variable(emitter, input, token.kind == ttGt);
            break;
        case ttDollar: case ttAt:
            kind = token.kind;
            if (!input->next(input->context, &token) || token.kind != ttName ||
                input->opcode(input->context, token.text, token.length))
                mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_SYNTAX);
            else if (kind == ttAt)
                mm_script_emit_label(emitter, token.text, token.length);
            else
                mm_script_emit_label_reference(emitter, token.text, token.length);
            break;
        default:
            mm_script_emitter_error(emitter, MM_SCRIPT_COMPILE_SYNTAX);
            break;
        }
    }
}
