/* Interpreter dispatch converted from Kauai screxe.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/script_vm.h"
#include "mm/script_opcodes.h"
#include "mm/wide.h"
#include <string.h>

int mm_script_vm_init(MMScriptVM *vm, const MMStorageAllocator *allocator, int trash_unused,
                      const MMScriptVMServices *services, void *host)
{
    int status;
    memset(vm, 0, sizeof(*vm));
    status = mm_script_stack_init(&vm->stack, allocator, trash_unused);
    if (status != MM_STORAGE_OK)
        return status;
    status = mm_script_variables_init(&vm->locals, allocator);
    vm->services = services;
    vm->host = host;
    return status;
}

void mm_script_vm_dispose(MMScriptVM *vm)
{
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

int mm_script_vm_attach(MMScriptVM *vm, MMCompiledScript *script, unsigned current, unsigned minimum)
{
    MMU32 version, written, compatible;
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
    else if (!vm->word_count)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_VERSION);
    else
    {
        version = (MMU32)mm_script_word(script, 0);
        written = version >> 16;
        compatible = version & 0xffffU;
        if (written < minimum || written >= 0x7fffU || compatible > written || compatible > current)
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_VERSION);
    }
    if (vm->error)
        return 0;
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
        value = services->random(vm->host, count);
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
    else if (!services || !services->shuffle)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
    else if (opcode == kopShuffle)
        services->shuffle(vm->host, count, NULL);
    else if ((words = mm_script_vm_words(vm, count)) != NULL)
    {
        if (mm_storage_lock(&vm->stack.storage, 0) != MM_STORAGE_OK)
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
        else
        {
            services->shuffle(vm->host, count, words);
            mm_storage_unlock(&vm->stack.storage, 0);
        }
    }
}

static void execute(MMScriptVM *vm, unsigned opcode)
{
    const MMScriptVMServices *services = vm->services;
    MMI32 a, b, c;
    double ratio;

    switch (opcode)
    {
    case kopExit:
        vm->pc = vm->word_count;
        return;
    case kopPause:
        vm->paused = 1;
        return;
    case kopNextCard:
        if (services && services->next_card)
            mm_script_vm_push(vm, services->next_card(vm->host));
        else
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        return;
    default:
        if (opcode < kopAdd || opcode >= kopCopyStr)
        {
            if (!services || !services->opcode || !services->opcode(vm->host, opcode))
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
