/* Chunky lexer converted from Kauai chcm.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunky_lexer.h"
#include "mm/string_ops.h"
#include "mm/wide.h"
#include <string.h>

typedef struct Keyword
{
    const char *name;
    int token;
} Keyword;

static const Keyword keywords[] = {
    {"ITEM", ttItem}, {"FREE", ttFree}, {"VAR", ttVar},
    {"BYTE", ttModeByte}, {"SHORT", ttModeShort}, {"LONG", ttModeLong},
    {"CHUNK", ttChunk}, {"ENDCHUNK", ttEndChunk}, {"ADOPT", ttAdopt},
    {"CHILD", ttChild}, {"PARENT", ttParent}, {"BO", ttBo},
    {"OSK", ttOsk}, {"String", ttModeStn}, {"STZ", ttModeStz},
    {"ZString", ttModeSz}, {"PascalString", ttModeSt}, {"ALIGN", ttAlign},
    {"FILE", ttFile}, {"PACKEDFILE", ttPackedFile}, {"META", ttMeta},
    {"BITMAP", ttBitmap}, {"MASK", ttMask}, {"MIDI", ttMidi},
    {"SCRIPT", ttScript}, {"SCRIPTPF", ttScriptP}, {"DynamicArray", ttGl},
    {"AllocatedArray", ttAl}, {"GeneralGroup", ttGg}, {"AllocatedGroup", ttAg},
    {"StringTable_GST", ttGst}, {"AllocatedStringTable", ttAst}, {"MACBO", ttMacBo},
    {"WINBO", ttWinBo}, {"MACOSK", ttMacOsk}, {"WINOSK", ttWinOsk},
    {"LONER", ttLoner}, {"CURSOR", ttCursor}, {"PALETTE", ttPalette},
    {"PREPACKED", ttPrePacked}, {"PACK", ttPack}, {"PACKFMT", ttPackFmt},
    {"SUBFILE", ttSubFile}
};

int mm_chunky_lexer_init(MMChunkyLexer *lexer, unsigned unit_bytes, const MMStorageAllocator *allocator)
{
    if (!lexer || (unit_bytes != 1 && unit_bytes != 2))
        return 0;
    memset(lexer, 0, sizeof(*lexer));
    if (mm_storage_init(&lexer->storage, allocator) != MM_STORAGE_OK)
        return 0;
    lexer->variables.stride = sizeof(MMU32) + sizeof(MMI32);
    lexer->variables.free_count = -1;
    lexer->variables.unit_bytes = unit_bytes;
    lexer->variables.context = &lexer->storage;
    lexer->variables.reserve = mm_strings_reserve_storage;
    lexer->storage.min_grow[0] = 128 * unit_bytes;
    lexer->storage.min_grow[1] = 16 * lexer->variables.stride;
    return 1;
}

void mm_chunky_lexer_dispose(MMChunkyLexer *lexer)
{
    mm_storage_dispose(&lexer->storage);
    memset(&lexer->variables, 0, sizeof(lexer->variables));
}

static int keyword(const MMLexText *name, unsigned unit)
{
    size_t i;
    for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i)
        if (mm_string_equals_ascii(keywords[i].name, &name->units, name->length, unit))
            return keywords[i].token;
    return ttName;
}

static int compare_names(void *context, const void *a, MMU32 a_len, const void *b, MMU32 b_len)
{
    MMU32 order = mm_string_internal_compare(context, a, a_len, b, b_len);
    return order == MM_STRING_LESS ? -1 : order == MM_STRING_GREATER;
}

static int find(const MMStringTable *variables, const MMLexText *name, MMU32 *position)
{
    MMStringOps rules = {0};
    rules.unit = variables->unit_bytes;
    return mm_strings_find(variables, &name->units, name->length, compare_names, &rules, position);
}

static unsigned char *number_slot(const MMStringTable *variables, MMU32 position)
{
    return variables->entries + position * variables->stride + sizeof(MMU32);
}

static MMI32 number_at(const MMStringTable *variables, MMU32 position)
{
    MMI32 value;
    memcpy(&value, number_slot(variables, position), sizeof(value));
    return value;
}

static int update_value(int operation, MMI32 *value, MMI32 operand)
{
    MMU32 left = (MMU32)*value, right = (MMU32)operand;
    switch (operation)
    {
    case ttAssign: left = right; break;
    case ttAAdd: left += right; break;
    case ttASub: left -= right; break;
    case ttAMul: left *= right; break;
    case ttABOr: left |= right; break;
    case ttABAnd: left &= right; break;
    case ttABXor: left ^= right; break;
    case ttAShr: left >>= right & 31U; break;
    case ttAShl: left <<= right & 31U; break;
    case ttADiv:
    case ttAMod:
        return mm_word_divide(*value, operand, operation == ttAMod, value) == MM_WORD_DIVIDED;
    default: return 0;
    }
    *value = mm_signed_word(left);
    return 1;
}

static int read_operand(const MMStringTable *variables, MMLexer *base, MMLexToken *token,
                        MMLexNumberReader read_number, void *context)
{
    MMU32 position;
    int negative = 0;
    while (mm_lexer_next(base, token, read_number, context))
    {
        if (token->kind == ttSub)
        {
            negative = !negative;
            continue;
        }
        if (token->kind == ttName && find(variables, &token->text, &position))
        {
            token->number = number_at(variables, position);
            token->kind = ttLong;
        }
        if (token->kind != ttLong)
            break;
        if (negative)
            token->number = mm_signed_word(0U - (MMU32)token->number);
        return 1;
    }
    return 0;
}

static int apply_set(MMStringTable *variables, MMLexer *base, MMLexToken *token,
                     MMLexNumberReader read_number, void *context)
{
    MMI32 value = 0;
    MMU32 position;
    int saved, operation;
    if (!mm_lexer_next(base, token, read_number, context) || token->kind != ttName)
        return 0;
    if ((saved = find(variables, &token->text, &position)) != 0)
        value = number_at(variables, position);
    else
        saved = mm_strings_insert(variables, position, &token->text.units, token->text.length, &value);

    if (!mm_lexer_next(base, token, read_number, context))
        return 0;
    operation = token->kind;
    if (operation == ttInc)
        value = mm_signed_word((MMU32)value + 1);
    else if (operation == ttDec)
        value = mm_signed_word((MMU32)value - 1);
    else if (operation < ttAssign || operation > ttAShl ||
             !read_operand(variables, base, token, read_number, context) ||
             !update_value(operation, &value, token->number))
        return 0;
    if (!saved)
        return 0;
    memcpy(number_slot(variables, position), &value, sizeof(value));
    return 1;
}

int mm_chunky_lexer_next(MMChunkyLexer *lexer, MMLexer *base, MMLexToken *token,
                         MMLexNumberReader read_number, void *context)
{
    MMStringTable *variables = &lexer->variables;
    unsigned unit = variables->unit_bytes;
    MMU32 position;
    while (mm_lexer_next(base, token, read_number, context))
    {
        if (token->kind != ttName)
            return 1;
        token->kind = keyword(&token->text, unit);
        if (token->kind != ttName)
            return 1;
        if (!mm_string_equals_ascii("SET", &token->text.units, token->text.length, unit))
        {
            if (find(variables, &token->text, &position))
            {
                token->kind = ttLong;
                token->number = number_at(variables, position);
            }
            return 1;
        }
        if (!apply_set(variables, base, token, read_number, context))
        {
            token->kind = ttError;
            return 1;
        }
    }
    return 0;
}

int mm_chunky_clean_token(MMChunkyFormat *format, const MMChunkyFormats *codes,
                          int kind, MMI32 *value, int *sign)
{
    switch (kind)
    {
    case ttLong:
        if (*sign == 1)
            *value = mm_signed_word(0U - (MMU32)*value);
        *sign = 0;
        return 1;
    default:
        return 1;
    case ttSub: *sign = *sign == 1 ? 2 : 1; break;
    case ttModeStn: format->string_mode = smStn; break;
    case ttModeStz: format->string_mode = smStz; break;
    case ttModeSz: format->string_mode = smSz; break;
    case ttModeSt: format->string_mode = smSt; break;
    case ttModeByte: format->number_bytes = 1; break;
    case ttModeShort: format->number_bytes = 2; break;
    case ttModeLong: format->number_bytes = 4; break;
    case ttMacBo: format->byte_order = codes->mac_order; break;
    case ttWinBo: format->byte_order = codes->windows_order; break;
    case ttMacOsk: format->os_kind = codes->mac_os_kind; break;
    case ttWinOsk: format->os_kind = codes->windows_os_kind; break;
    }
    return 0;
}

int mm_chunky_write_number(unsigned width, int swap, MMI32 value, unsigned char *out)
{
    const unsigned short one = 1;
    int little = (*(const unsigned char *)&one != 0) != !!swap;
    MMU32 word = (MMU32)value;
    unsigned i;
    for (i = 0; i < width; ++i)
    {
        out[little ? i : width - 1 - i] = (unsigned char)word;
        word >>= 8;
    }
    if (width == 1 && (value < -128 || value > 255))
        return MM_CHUNKY_NUMBER_BYTE_RANGE;
    /* The legacy short writer excludes -32768 from its signed range. */
    if (width == 2 && (value < -32767 || value > 65535))
        return MM_CHUNKY_NUMBER_SHORT_RANGE;
    return MM_CHUNKY_NUMBER_OK;
}
