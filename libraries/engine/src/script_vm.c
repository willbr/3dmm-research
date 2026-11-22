/* Interpreter dispatch converted from Kauai screxe.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/script_vm.h"
#include "mm/script_kidspace.h"
#include "mm/script_opcodes.h"
#include "mm/string_format.h"
#include "mm/wide.h"
#include <string.h>

int mm_script_vm_init(MMScriptVM *vm, const MMStorageAllocator *allocator, int trash_unused,
                      MMScriptStrings *strings, const MMScriptVMServices *services, void *host)
{
    int status;
    memset(vm, 0, sizeof(*vm));
    status = mm_script_stack_init(&vm->stack, allocator, trash_unused);
    if (status != MM_STORAGE_OK)
        return status;
    status = mm_script_variables_init(&vm->locals, allocator);
    vm->strings = strings;
    vm->services = services;
    vm->host = host;
    return status;
}

void mm_script_vm_dispose(MMScriptVM *vm)
{
    MMU32 index;
    MMI32 id;
    if (vm->script && vm->strings && vm->locals.count)
        for (index = mm_script_literal_count(vm->script); index-- > 0;)
            if (mm_script_variables_get(&vm->locals, 0, index, &id))
                mm_script_strings_delete(vm->strings, id);
    mm_script_stack_dispose(&vm->stack);
    mm_script_variables_dispose(&vm->locals);
    mm_script_release(vm->script);
    vm->script = NULL;
    vm->paused = 0;
}

void mm_script_vm_fail(MMScriptVM *vm, int reason)
{
    if (vm->error)
        return;
    vm->error = reason;
    if (vm->services && vm->services->error)
        vm->services->error(vm->host, reason);
}

static int add_parameters(MMScriptVM *vm, const unsigned char *words, MMU32 count)
{
    MMU32 high, low, i;
    MMI32 value;
    if (!count)
        return 1;
    mm_script_variable_name("_cparm", 6, 1, &high, &low);
    if (!mm_script_variables_set(&vm->locals, high, low, (MMI32)count))
        return 0;
    mm_script_variable_name("_parm", 5, 1, &high, &low);
    for (i = 0; i < count; ++i)
    {
        memcpy(&value, words + i * sizeof(value), sizeof(value));
        if (!mm_script_variables_set(&vm->locals, (i << 16) | high, low, value))
            return 0;
    }
    return 1;
}

static int add_literals(MMScriptVM *vm)
{
    MMU32 index, count;
    MMI32 id;
    const void *text;
    if (!mm_script_has_literals(vm->script))
        return 1;
    if (!vm->strings || vm->strings->unit_bytes != mm_script_unit_bytes(vm->script))
        return 0;
    for (index = 0; index < mm_script_literal_count(vm->script); ++index)
    {
        if (!mm_script_literal(vm->script, index, &text, &count) ||
            !mm_script_strings_add(vm->strings, &id, text, count))
            return 0;
        if (!mm_script_variables_set(&vm->locals, 0, index, id))
        {
            mm_script_strings_delete(vm->strings, id);
            return 0;
        }
    }
    return 1;
}

int mm_script_vm_attach(MMScriptVM *vm, MMCompiledScript *script, unsigned current, unsigned minimum,
                        const void *parameters, MMU32 parameter_count)
{
    if (vm->script)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_STATE);
        return 0;
    }
    vm->error = MM_SCRIPT_ERROR_NONE;
    vm->result = 0;
    vm->pc = 0;
    vm->script = script;
    vm->word_count = 0;
    if (script)
    {
        mm_script_retain(script);
        vm->word_count = mm_script_word_count(script);
    }
    if (mm_script_stack_reserve(&vm->stack, 10) != MM_STORAGE_OK)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
    else if (!vm->word_count || !mm_script_version_readable((MMU32)mm_script_word(script, 0), current, minimum))
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_VERSION);
    if (vm->error)
        return 0;
    if ((parameter_count && !parameters) || parameter_count > MM_STORAGE_LIMIT / sizeof(MMI32))
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
        return 0;
    }
    if (!add_parameters(vm, parameters, parameter_count) || !add_literals(vm))
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
        return 0;
    }
    vm->pc = 1;
    vm->paused = 1;
    return 1;
}

void mm_script_vm_push(MMScriptVM *vm, MMI32 word)
{
    if (!vm->error && mm_script_stack_push(&vm->stack, &word, 1) != MM_STORAGE_OK)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
}

MMI32 mm_script_vm_pop(MMScriptVM *vm)
{
    MMI32 word;
    if (!vm->error && mm_script_stack_pop(&vm->stack, &word) == MM_STORAGE_OK)
        return word;
    mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
    return 0;
}

const void *mm_script_vm_words(MMScriptVM *vm, MMI32 count)
{
    const void *words;
    if (vm->error)
        return NULL;
    words = mm_script_stack_top(&vm->stack, count);
    if (!words)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
    return words;
}

static void stack_status(MMScriptVM *vm, int status)
{
    if (status != MM_STORAGE_OK)
        mm_script_vm_fail(vm, status == MM_STORAGE_ARGUMENT ? MM_SCRIPT_ERROR_OPERAND : MM_SCRIPT_ERROR_RESOURCE);
}

void mm_script_vm_drop(MMScriptVM *vm, MMI32 count)
{
    if (!vm->error)
        stack_status(vm, mm_script_stack_drop(&vm->stack, count));
}

static void execute_variable(MMScriptVM *vm, unsigned opcode, MMU32 high, MMU32 low)
{
    const MMScriptVMServices *services = vm->services;
    MMScriptVariables *variables = &vm->locals;
    MMI32 value = 0;
    int scope;

    if (opcode >= kopMinArray && opcode < kopLimArray)
    {
        value = mm_script_vm_pop(vm);
        if (vm->error)
            return;
        high = (high & 0xffffU) | ((MMU32)value << 16);
        opcode = opcode - kopPushLocArray + kopPushLocVar;
    }
    switch (opcode)
    {
    case kopPushLocVar: case kopPopLocVar:
        scope = 0;
        break;
    case kopPushThisVar: case kopPopThisVar:
        scope = MM_SCRIPT_SCOPE_THIS;
        break;
    case kopPushGlobalVar: case kopPopGlobalVar:
        scope = MM_SCRIPT_SCOPE_GLOBAL;
        break;
    case kopPushRemoteVar: case kopPopRemoteVar:
        scope = MM_SCRIPT_SCOPE_REMOTE;
        value = mm_script_vm_pop(vm);
        if (vm->error)
            return;
        break;
    default:
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        return;
    }

    if (scope)
        variables = services && services->variables ? services->variables(vm->host, scope, value) : NULL;
    if ((opcode - kopPushLocVar) % 2)
    {
        value = mm_script_vm_pop(vm);
        if (!variables)
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
        else if (!vm->error && !mm_script_variables_set(variables, high, low, value))
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
    }
    else
    {
        if (!mm_script_variables_get(variables, high, low, &value))
        {
            if (services && services->missing_variable)
                services->missing_variable(vm->host, high, low);
            value = 0;
        }
        mm_script_vm_push(vm, value);
    }
}

static int compare(unsigned opcode, MMI32 left, MMI32 right)
{
    switch (opcode)
    {
    case kopEq: case kopGoEq: return left == right;
    case kopNe: case kopGoNe: return left != right;
    case kopGt: case kopGoGt: return left > right;
    case kopLt: case kopGoLt: return left < right;
    case kopGe: case kopGoGe: return left >= right;
    default: return left <= right;
    }
}

static MMU32 magnitude(MMI32 word)
{
    return word < 0 ? 0U - (MMU32)word : (MMU32)word;
}

static void divide(MMScriptVM *vm, unsigned opcode, MMI32 right)
{
    MMI32 left;
    MMU32 value;
    int negative;
    if (!right)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
        return;
    }
    left = mm_script_vm_pop(vm);
    if (left == (-2147483647 - 1) && right == -1)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
        return;
    }
    /* Use unsigned operands to specify truncation toward zero on C89 hosts. */
    if (opcode == kopDiv)
    {
        value = magnitude(left) / magnitude(right);
        negative = (left < 0) != (right < 0);
    }
    else
    {
        value = magnitude(left) % magnitude(right);
        negative = left < 0;
    }
    mm_script_vm_push(vm, mm_signed_word(negative ? 0U - value : value));
}

static void jump(MMScriptVM *vm, MMI32 label, int taken)
{
    MMU32 encoded = (MMU32)label;
    MMU32 target = encoded & 0x00ffffffUL;
    if ((encoded >> 24) != MM_SCRIPT_LABEL_BYTE || target > vm->word_count)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
    else if (taken)
        vm->pc = target;
}

static void random_word(MMScriptVM *vm, unsigned opcode, MMI32 count)
{
    const MMScriptVMServices *services = vm->services;
    MMI32 value;
    if (count <= 0)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
    else if (!services || !services->random)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
    else
    {
        value = (MMI32)mm_random_bounded(services->random, (MMU32)count);
        if (opcode == kopRndList)
            stack_status(vm, mm_script_stack_select(&vm->stack, count, value));
        else
            mm_script_vm_push(vm, value);
    }
}

static void shuffle(MMScriptVM *vm, unsigned opcode, MMI32 count)
{
    const MMScriptVMServices *services = vm->services;
    const void *words = NULL;
    if (count <= 0)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
    else if (!services || !services->deck)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
    else if (opcode == kopShuffle)
        mm_shuffle_set(services->deck, count, NULL);
    else if ((words = mm_script_vm_words(vm, count)) != NULL)
    {
        if (mm_storage_lock(&vm->stack.storage, 0) != MM_STORAGE_OK)
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
        else
        {
            mm_shuffle_set(services->deck, count, words);
            mm_storage_unlock(&vm->stack.storage, 0);
        }
    }
}

typedef union ScriptText
{
    unsigned char narrow[257];
    unsigned short wide[257];
} ScriptText;

static void string_notice(MMScriptVM *vm, int reason, MMI32 value)
{
    if (vm->services && vm->services->string_notice)
        vm->services->string_notice(vm->host, reason, value);
}

static MMU32 read_string(MMScriptVM *vm, MMI32 id, const void **text, int warn)
{
    MMU32 count;
    if (!mm_script_strings_get(vm->strings, id, text, &count) && warn)
        string_notice(vm, MM_SCRIPT_STRING_MISSING, id);
    return count;
}

static void write_string(MMScriptVM *vm, MMI32 id, const ScriptText *text)
{
    MMU32 count;
    const void *data;
    if (vm->strings->unit_bytes == 1)
    {
        count = text->narrow[0];
        data = text->narrow + 1;
    }
    else
    {
        count = text->wide[0];
        data = text->wide + 1;
    }
    if (!mm_script_strings_put(vm->strings, id, data, count))
    {
        string_notice(vm, MM_SCRIPT_STRING_STORE_FAILED, id);
        id = -1;
    }
    mm_script_vm_push(vm, id);
}

static void merge_strings(MMScriptVM *vm, MMI32 number, MMU32 scope)
{
    MMScriptCollection *table;
    if (vm->error)
        return;
    if (!vm->services || !vm->services->string_resource)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
        return;
    }
    table = vm->services->string_resource(vm->host, (MMU32)number, scope);
    if (!table)
    {
        if (!vm->error)
            string_notice(vm, MM_SCRIPT_STRING_RESOURCE_MISSING, number);
        return;
    }
    if (!mm_script_registry_import(table, vm->strings))
        string_notice(vm, MM_SCRIPT_STRING_MERGE_FAILED, number);
    mm_script_collection_priority(table, MM_RESOURCE_TOSS_PRIORITY);
    mm_script_collection_release(table);
}

static int decimal_argument(void *context, unsigned control, MMStringFormatArgument *argument)
{
    (void)control;
    argument->number = (MMU32)*(const MMI32 *)context;
    return 1;
}

static void number_string(const MMStringValue *output, MMI32 number)
{
    static const unsigned char narrow[] = {'%', 'd'};
    static const unsigned short wide[] = {'%', 'd'};
    MMStringFormatArguments args;
    const void *pattern = output->unit == 1 ? (const void *)narrow : (const void *)wide;
    args.context = &number;
    args.next = decimal_argument;
    args.bad_format = NULL;
    mm_string_format(output, pattern, 2, &args);
}

static void execute_string(MMScriptVM *vm, unsigned opcode)
{
    ScriptText buffer;
    MMStringValue output;
    const void *text;
    MMI32 source, destination, start = 0, limit = 255, second, value;
    MMU32 count;

    source = mm_script_vm_pop(vm);
    if (vm->error)
        return;
    if (!vm->strings)
    {
        if (opcode == kopMergeStrs)
            string_notice(vm, MM_SCRIPT_STRING_NO_REGISTRY, source);
        mm_script_vm_fail(vm, opcode == kopMergeStrs ? MM_SCRIPT_ERROR_RESOURCE : MM_SCRIPT_ERROR_OPERAND);
        return;
    }
    output.buffer = &buffer;
    output.unit = vm->strings->unit_bytes;
    output.context = NULL;
    output.check_copy = NULL;

    switch (opcode)
    {
    case kopCopySubStr:
        start = mm_script_vm_pop(vm);
        limit = mm_script_vm_pop(vm);
        /* fall through */
    case kopCopyStr:
        destination = mm_script_vm_pop(vm);
        if (vm->error)
            break;
        count = read_string(vm, source, &text, 1);
        mm_string_value_set(&output, text, (MMI32)count);
        if (start > 0)
            mm_string_value_delete(&output, 0, start);
        if (limit < 0)
            string_notice(vm, MM_SCRIPT_STRING_BAD_SLICE, limit);
        else
            mm_string_value_delete(&output, limit, 255);
        write_string(vm, destination, &buffer);
        break;
    case kopMoveStr:
        destination = mm_script_vm_pop(vm);
        if (!vm->error)
            mm_script_vm_push(vm, mm_script_strings_move(vm->strings, source, destination) ? destination : -1);
        break;
    case kopNukeStr:
        mm_script_strings_delete(vm->strings, source);
        break;
    case kopMergeStrs:
        merge_strings(vm, source, (MMU32)mm_script_vm_pop(vm));
        break;
    case kopNumToStr:
        destination = mm_script_vm_pop(vm);
        if (vm->error)
            break;
        number_string(&output, source);
        write_string(vm, destination, &buffer);
        break;
    case kopStrToNum:
        second = mm_script_vm_pop(vm);
        destination = mm_script_vm_pop(vm);
        if (vm->error)
            break;
        count = read_string(vm, source, &text, 0);
        if (!count)
            value = second;
        else
        {
            mm_string_value_set(&output, text, (MMI32)count);
            if (!mm_string_value_number(&output, 0, &value))
                value = destination;
        }
        mm_script_vm_push(vm, value);
        break;
    case kopConcatStrs:
        second = mm_script_vm_pop(vm);
        destination = mm_script_vm_pop(vm);
        if (vm->error)
            break;
        count = read_string(vm, source, &text, 1);
        mm_string_value_set(&output, text, (MMI32)count);
        count = read_string(vm, second, &text, 1);
        mm_string_value_append(&output, text, (MMI32)count);
        write_string(vm, destination, &buffer);
        break;
    case kopLenStr:
        mm_script_vm_push(vm, (MMI32)read_string(vm, source, &text, 1));
        break;
    default:
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        break;
    }
}

static void execute(MMScriptVM *vm, unsigned opcode)
{
    const MMScriptVMServices *services = vm->services;
    MMI32 a, b, c;
    double ratio;

    if (opcode >= kopCopyStr && opcode < kopLimSccb && opcode != kopScaleTime)
    {
        execute_string(vm, opcode);
        return;
    }
    switch (opcode)
    {
    case kopExit:
        vm->pc = vm->word_count;
        return;
    case kopPause:
        vm->paused = 1;
        return;
    case kopNextCard:
        if (services && services->deck)
            mm_script_vm_push(vm, mm_shuffle_next(services->deck, 0));
        else
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        return;
    case kopScaleTime:
        if (services && services->scale_time)
            services->scale_time(vm->host, (MMU32)mm_script_vm_pop(vm));
        else
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        return;
    default:
        if (opcode < kopAdd || opcode >= kopCopyStr)
        {
            if (vm->kidspace && opcode >= kopCreateChildGob && opcode < kopLimSccg &&
                mm_kidspace_script_execute(vm, opcode))
                return;
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
            return;
        }
    }

    a = mm_script_vm_pop(vm);
    if (vm->error)
        return;
    switch (opcode)
    {
    case kopAdd:
        mm_script_vm_push(vm, mm_signed_word((MMU32)mm_script_vm_pop(vm) + (MMU32)a));
        break;
    case kopSub:
        mm_script_vm_push(vm, mm_signed_word((MMU32)mm_script_vm_pop(vm) - (MMU32)a));
        break;
    case kopMul:
        mm_script_vm_push(vm, mm_signed_word((MMU32)mm_script_vm_pop(vm) * (MMU32)a));
        break;
    case kopDiv: case kopMod:
        divide(vm, opcode, a);
        break;
    case kopNeg:
        mm_script_vm_push(vm, mm_signed_word(0U - (MMU32)a));
        break;
    case kopInc:
        mm_script_vm_push(vm, mm_signed_word((MMU32)a + 1U));
        break;
    case kopDec:
        mm_script_vm_push(vm, mm_signed_word((MMU32)a - 1U));
        break;
    case kopShr:
        mm_script_vm_push(vm, mm_signed_word((MMU32)mm_script_vm_pop(vm) >> ((MMU32)a & 31U)));
        break;
    case kopShl:
        mm_script_vm_push(vm, mm_signed_word((MMU32)mm_script_vm_pop(vm) << ((MMU32)a & 31U)));
        break;
    case kopBOr:
        mm_script_vm_push(vm, mm_signed_word((MMU32)mm_script_vm_pop(vm) | (MMU32)a));
        break;
    case kopBAnd:
        mm_script_vm_push(vm, mm_signed_word((MMU32)mm_script_vm_pop(vm) & (MMU32)a));
        break;
    case kopBXor:
        mm_script_vm_push(vm, mm_signed_word((MMU32)mm_script_vm_pop(vm) ^ (MMU32)a));
        break;
    case kopBNot:
        mm_script_vm_push(vm, mm_signed_word(~(MMU32)a));
        break;
    case kopLXor:
        mm_script_vm_push(vm, (mm_script_vm_pop(vm) != 0) != (a != 0));
        break;
    case kopLNot:
        mm_script_vm_push(vm, !a);
        break;
    case kopEq: case kopNe: case kopGt: case kopLt: case kopGe: case kopLe:
        mm_script_vm_push(vm, compare(opcode, mm_script_vm_pop(vm), a));
        break;
    case kopAbs:
        mm_script_vm_push(vm, mm_signed_word(magnitude(a)));
        break;
    case kopRnd: case kopRndList:
        random_word(vm, opcode, a);
        break;
    case kopMulDiv:
        b = mm_script_vm_pop(vm);
        c = mm_script_vm_pop(vm);
        if (!c)
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
        else
        {
            ratio = (double)a * b / c;
            if (ratio < -2147483647.0 || ratio > 2147483647.0)
                mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
            else
                mm_script_vm_push(vm, (MMI32)ratio);
        }
        break;
    case kopDup:
        mm_script_vm_push(vm, a);
        mm_script_vm_push(vm, a);
        break;
    case kopPop:
        break;
    case kopSwap:
        b = mm_script_vm_pop(vm);
        mm_script_vm_push(vm, a);
        mm_script_vm_push(vm, b);
        break;
    case kopRot:
        b = mm_script_vm_pop(vm);
        if (!vm->error)
            stack_status(vm, mm_script_stack_rotate(&vm->stack, b, a));
        break;
    case kopRev:
        stack_status(vm, mm_script_stack_reverse(&vm->stack, a));
        break;
    case kopDupList:
        stack_status(vm, mm_script_stack_duplicate(&vm->stack, a));
        break;
    case kopPopList:
        mm_script_vm_drop(vm, a);
        break;
    case kopSelect:
        b = mm_script_vm_pop(vm);
        if (!vm->error)
            stack_status(vm, mm_script_stack_select(&vm->stack, a, b));
        break;
    case kopGoEq: case kopGoNe: case kopGoGt: case kopGoLt: case kopGoGe: case kopGoLe:
        a = compare(opcode, mm_script_vm_pop(vm), a);
        b = mm_script_vm_pop(vm);
        jump(vm, b, a);
        break;
    case kopGoZ: case kopGoNz:
        b = mm_script_vm_pop(vm);
        jump(vm, b, opcode == kopGoZ ? a == 0 : a != 0);
        break;
    case kopGo:
        jump(vm, a, 1);
        break;
    case kopReturn:
        vm->pc = vm->word_count;
        /* fall through */
    case kopSetReturn:
        vm->result = a;
        break;
    case kopShuffle: case kopShuffleList:
        shuffle(vm, opcode, a);
        break;
    case kopMatch:
        b = mm_script_vm_pop(vm);
        c = mm_script_vm_pop(vm);
        if (!vm->error)
            stack_status(vm, mm_script_stack_match(&vm->stack, a, b, c));
        break;
    default:
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        break;
    }
}

int mm_script_vm_resume(MMScriptVM *vm)
{
    MMU32 word, count, opcode, next, low;
    if (!vm->script || !vm->paused || vm->error)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_STATE);
    vm->paused = 0;
    while (vm->pc < vm->word_count && !vm->error && !vm->paused)
    {
        word = (MMU32)mm_script_word(vm->script, vm->pc++);
        count = (word >> 16) & 0xffU;
        if (count > vm->word_count - vm->pc)
        {
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_INSTRUCTION);
            break;
        }

        opcode = word >> 24;
        if (opcode)
        {
            if (!count)
            {
                mm_script_vm_fail(vm, MM_SCRIPT_ERROR_VARIABLE);
                break;
            }
            low = (MMU32)mm_script_word(vm->script, vm->pc++);
            --count;
            next = vm->pc;
            execute_variable(vm, opcode, word & 0xffffU, low);
        }
        else
        {
            next = vm->pc;
            opcode = word & 0xffffU;
            if (opcode != opNil)
                execute(vm, opcode);
        }

        if (!vm->error && count && next == vm->pc)
        {
            if (mm_script_stack_push(&vm->stack, mm_script_word_span(vm->script, next, count), count) != MM_STORAGE_OK)
                mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
            else
                vm->pc += count;
        }
    }
    if (vm->pc >= vm->word_count || vm->error)
        vm->paused = 0;
    return !vm->error;
}
