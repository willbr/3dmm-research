/* Stack and variable algorithms converted from Kauai screxe.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/script_runtime.h"
#include "mm/collections.h"
#include <string.h>

typedef struct MMVariableRow
{
    MMU32 high, low;
    MMI32 word;
} MMVariableRow;

static MMArray array_view(MMCollectionStorage *storage, MMU32 width, MMU32 count, int trash_unused)
{
    MMArray array;
    array.storage = storage;
    array.width = width;
    array.count = count;
    array.free_count = -1;
    array.allocated = 0;
    array.trash_unused = trash_unused;
    return array;
}

static MMArray stack_view(MMScriptStack *stack)
{
    return array_view(&stack->storage, sizeof(MMI32), stack->count, stack->trash_unused);
}

int mm_script_stack_init(MMScriptStack *stack, const MMStorageAllocator *allocator, int trash_unused)
{
    int result = mm_storage_init(&stack->storage, allocator);
    if (result != MM_STORAGE_OK)
        return result;
    stack->storage.min_grow[0] = 10 * sizeof(MMI32);
    stack->count = 0;
    stack->trash_unused = trash_unused;
    return MM_STORAGE_OK;
}

void mm_script_stack_dispose(MMScriptStack *stack)
{
    if (mm_storage_dispose(&stack->storage) == MM_STORAGE_OK)
        stack->count = 0;
}

int mm_script_stack_reserve(MMScriptStack *stack, MMU32 additional)
{
    MMArray array = stack_view(stack);
    return mm_array_reserve(&array, additional, 0);
}

int mm_script_stack_push(MMScriptStack *stack, const void *words, MMU32 count)
{
    MMArray array = stack_view(stack);
    int result;
    if (count && !words)
        return MM_STORAGE_ARGUMENT;
    result = mm_array_insert(&array, array.count, words, count);
    stack->count = array.count;
    return result;
}

int mm_script_stack_pop(MMScriptStack *stack, MMI32 *word)
{
    MMArray array = stack_view(stack);
    int result = mm_array_pop(&array, word);
    stack->count = array.count;
    return result;
}

const void *mm_script_stack_top(const MMScriptStack *stack, MMI32 count)
{
    if (count <= 0 || (MMU32)count > stack->count)
        return NULL;
    return stack->storage.buffers[0].data + (stack->count - (MMU32)count) * sizeof(MMI32);
}

int mm_script_stack_drop(MMScriptStack *stack, MMI32 count)
{
    MMArray array = stack_view(stack);
    int result;
    if (count < 0 || (MMU32)count > stack->count)
        return MM_STORAGE_ARGUMENT;
    result = mm_array_set_count(&array, array.count - (MMU32)count);
    stack->count = array.count;
    return result;
}

int mm_script_stack_duplicate(MMScriptStack *stack, MMI32 count)
{
    MMArray array = stack_view(stack);
    unsigned char *end;
    MMU32 bytes;
    int result;
    if (!count)
        return MM_STORAGE_OK;
    if (count < 0 || (MMU32)count > stack->count)
        return MM_STORAGE_ARGUMENT;
    result = mm_array_set_count(&array, stack->count + (MMU32)count);
    if (result != MM_STORAGE_OK)
        return result;
    end = stack->storage.buffers[0].data + stack->count * sizeof(MMI32);
    bytes = (MMU32)count * sizeof(MMI32);
    memcpy(end, end - bytes, bytes);
    stack->count = array.count;
    return MM_STORAGE_OK;
}

static void reverse_words(unsigned char *words, MMI32 count)
{
    unsigned char *last = words + (count - 1) * sizeof(MMI32);
    MMI32 word;
    for (; words < last; words += sizeof(word), last -= sizeof(word))
    {
        memcpy(&word, words, sizeof(word));
        memcpy(words, last, sizeof(word));
        memcpy(last, &word, sizeof(word));
    }
}

int mm_script_stack_reverse(MMScriptStack *stack, MMI32 count)
{
    unsigned char *words;
    if (count == 0 || count == 1)
        return MM_STORAGE_OK;
    words = (unsigned char *)mm_script_stack_top(stack, count);
    if (!words)
        return MM_STORAGE_ARGUMENT;
    reverse_words(words, count);
    return MM_STORAGE_OK;
}

int mm_script_stack_rotate(MMScriptStack *stack, MMI32 count, MMI32 left)
{
    unsigned char *words;
    if (count == 0 || count == 1)
        return MM_STORAGE_OK;
    words = (unsigned char *)mm_script_stack_top(stack, count);
    if (!words)
        return MM_STORAGE_ARGUMENT;
    left %= count;
    if (left < 0)
        left += count;
    if (left)
    {
        reverse_words(words, left);
        reverse_words(words + left * sizeof(MMI32), count - left);
        reverse_words(words, count);
    }
    return MM_STORAGE_OK;
}

int mm_script_stack_select(MMScriptStack *stack, MMI32 count, MMI32 from_top)
{
    unsigned char *words = (unsigned char *)mm_script_stack_top(stack, count);
    if (!words || from_top < 0 || from_top >= count)
        return MM_STORAGE_ARGUMENT;
    memmove(words, words + (count - 1 - from_top) * sizeof(MMI32), sizeof(MMI32));
    return mm_script_stack_drop(stack, count - 1);
}

int mm_script_stack_match(MMScriptStack *stack, MMI32 pairs, MMI32 key, MMI32 fallback)
{
    unsigned char *words;
    MMI32 i, candidate;
    if (pairs <= 0 || (MMU32)pairs > stack->count / 2)
        return MM_STORAGE_ARGUMENT;
    words = (unsigned char *)mm_script_stack_top(stack, pairs * 2);
    for (i = pairs * 2 - 1; i > 0; i -= 2)
    {
        memcpy(&candidate, words + i * sizeof(candidate), sizeof(candidate));
        if (candidate == key)
        {
            memcpy(&fallback, words + (i - 1) * sizeof(fallback), sizeof(fallback));
            break;
        }
    }
    memcpy(words, &fallback, sizeof(fallback));
    return mm_script_stack_drop(stack, pairs * 2 - 1);
}

int mm_script_variables_init(MMScriptVariables *variables, const MMStorageAllocator *allocator)
{
    int result = mm_storage_init(&variables->storage, allocator);
    if (result != MM_STORAGE_OK)
        return result;
    variables->storage.min_grow[0] = 10 * sizeof(MMVariableRow);
    variables->count = 0;
    return MM_STORAGE_OK;
}

void mm_script_variables_dispose(MMScriptVariables *variables)
{
    if (mm_storage_dispose(&variables->storage) == MM_STORAGE_OK)
        variables->count = 0;
}

static int find_variable(const MMScriptVariables *variables, MMU32 high, MMU32 low,
                         MMU32 *index, MMVariableRow *row)
{
    MMU32 first = 0, limit = variables->count, mid;
    const unsigned char *data = variables->storage.buffers[0].data;
    while (first < limit)
    {
        mid = first + (limit - first) / 2;
        memcpy(row, data + mid * sizeof(*row), sizeof(*row));
        if (row->high == high && row->low == low)
        {
            *index = mid;
            return 1;
        }
        if (row->high < high || (row->high == high && row->low < low))
            first = mid + 1;
        else
            limit = mid;
    }
    *index = first;
    return 0;
}

int mm_script_variables_get(const MMScriptVariables *variables, MMU32 high, MMU32 low, MMI32 *word)
{
    MMVariableRow row;
    MMU32 index;
    if (!variables || !find_variable(variables, high, low, &index, &row))
        return 0;
    if (word)
        *word = row.word;
    return 1;
}

int mm_script_variables_set(MMScriptVariables *variables, MMU32 high, MMU32 low, MMI32 word)
{
    MMVariableRow row;
    MMU32 index;
    MMArray array;
    if (find_variable(variables, high, low, &index, &row))
    {
        row.word = word;
        memcpy(variables->storage.buffers[0].data + index * sizeof(row), &row, sizeof(row));
        return 1;
    }
    row.high = high;
    row.low = low;
    row.word = word;
    array = array_view(&variables->storage, sizeof(row), variables->count, 0);
    if (mm_array_insert(&array, index, &row, 1) != MM_STORAGE_OK)
        return 0;
    variables->count = array.count;
    return 1;
}
