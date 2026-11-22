/* Script listing converted from Kauai scrcom.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "script_compile_internal.h"
#include "mm/script_opcodes.h"
#include "mm/script_runtime.h"
#include "mm/string_format.h"
#include <string.h>

typedef union ListingText
{
    unsigned char narrow[257];
    unsigned short wide[257];
} ListingText;

typedef struct Listing
{
    MMScriptTextWrite write;
    void *context;
    unsigned unit;
} Listing;

static const void *native_ascii(unsigned unit, const char *text, MMU32 length, unsigned short *wide)
{
    MMU32 i;
    if (unit == 1)
        return text;
    for (i = 0; i < length; ++i)
        wide[i] = (unsigned char)text[i];
    return wide;
}

static void write_ascii(const Listing *out, const char *text, int end_line)
{
    unsigned short wide[256];
    MMU32 length = (MMU32)strlen(text);
    if (out->write)
        out->write(out->context, native_ascii(out->unit, text, length, wide), length, end_line);
}

static void write_text(const Listing *out, const ListingText *text)
{
    MMU32 length = out->unit == 1 ? text->narrow[0] : text->wide[0];
    if (out->write)
        out->write(out->context, (const unsigned char *)text + out->unit, length, 0);
}

static int format_number(void *context, unsigned control, MMStringFormatArgument *argument)
{
    const MMU32 **next = context;
    (void)control;
    argument->number = *(*next)++;
    return 1;
}

static void write_numbers(const Listing *out, const char *format, MMU32 first, MMU32 second)
{
    ListingText text;
    MMStringValue value;
    MMStringFormatArguments arguments;
    MMU32 words[2];
    const MMU32 *next = words;
    unsigned short wide[32];
    MMU32 length = (MMU32)strlen(format);

    words[0] = first;
    words[1] = second;
    value.buffer = &text;
    value.unit = out->unit;
    value.context = NULL;
    value.check_copy = NULL;
    arguments.context = &next;
    arguments.next = format_number;
    arguments.bad_format = NULL;
    mm_string_format(&value, native_ascii(out->unit, format, length, wide), (MMI32)length, &arguments);
    write_text(out, &text);
}

static int write_variable(const Listing *out, const MMCompiledScript *script, unsigned opcode, MMU32 high, MMU32 low)
{
    static const char *const prefixes[] = {"<", ">", "<.", ">.", "<::", ">::", "<~", ">~"};
    const void *literal;
    const char *scope;
    MMU32 length;
    ListingText text;
    MMStringValue value;
    char prefix[5];
    char name[MM_SCRIPT_VARIABLE_TEXT_CAPACITY + 1];
    int array = opcode >= kopMinArray && opcode < kopLimArray;
    if (!high)
    {
        if (opcode != kopPushLocVar)
            return MM_SCRIPT_DISASSEMBLE_VARIABLE;
        if (!mm_script_literal(script, low, &literal, &length))
            return MM_SCRIPT_DISASSEMBLE_LITERAL;
        value.buffer = &text;
        value.unit = out->unit;
        value.context = NULL;
        value.check_copy = NULL;
        mm_string_value_set(&value, literal, (MMI32)length);
        mm_string_value_expand(&value);
        write_ascii(out, "\"", 0);
        write_text(out, &text);
        write_ascii(out, "\" ", 0);
        return MM_SCRIPT_DISASSEMBLED;
    }

    if (array)
        opcode -= kopPushLocArray - kopPushLocVar;
    if (opcode < kopPushLocVar || opcode > kopPopRemoteVar)
        return MM_SCRIPT_DISASSEMBLE_VARIABLE_OPCODE;
    scope = prefixes[opcode - kopPushLocVar];
    length = (MMU32)strlen(scope);
    memcpy(prefix, scope, length);
    if (array)
        prefix[length++] = '&';
    prefix[length] = '\0';
    write_ascii(out, prefix, 0);
    length = mm_script_variable_text(high, low, name);
    name[length++] = ' ';
    name[length] = '\0';
    write_ascii(out, name, 0);
    return MM_SCRIPT_DISASSEMBLED;
}

int mm_script_disassemble(const MMCompiledScript *script, int dialect, unsigned current, unsigned minimum,
                          MMScriptTextWrite write, void *context)
{
    Listing out;
    MMU32 words, index, count, word, opcode;
    const char *name;
    int result;
    if (!script || !(words = mm_script_word_count(script)))
        return MM_SCRIPT_DISASSEMBLE_NO_VERSION;
    if (!mm_script_version_readable((MMU32)mm_script_word(script, 0), current, minimum))
        return MM_SCRIPT_DISASSEMBLE_VERSION;
    out.write = write;
    out.context = context;
    out.unit = mm_script_unit_bytes(script);

    for (index = 1;;)
    {
        write_numbers(&out, "@L_%04x ", index, 0);
        if (index == words)
            break;
        word = (MMU32)mm_script_word(script, index++);
        count = (word >> 16) & 0xffU;
        if (count > words - index)
            return MM_SCRIPT_DISASSEMBLE_INSTRUCTION;
        opcode = word >> 24;
        if (opcode)
        {
            if (!count)
                return MM_SCRIPT_DISASSEMBLE_VARIABLE_INSTRUCTION;
            --count;
            result = write_variable(&out, script, (unsigned)opcode, word & 0xffffU,
                                    (MMU32)mm_script_word(script, index++));
            if (result != MM_SCRIPT_DISASSEMBLED)
                return result;
        }
        else if ((opcode = word & 0xffffU) != opNil)
        {
            name = mm_script_opcode_name(dialect, opcode);
            if (!name)
                return MM_SCRIPT_DISASSEMBLE_OPCODE;
            write_ascii(&out, name, 0);
            write_ascii(&out, " ", 0);
        }
        while (count--)
        {
            word = (MMU32)mm_script_word(script, index++);
            if (word >> 24 == MM_SCRIPT_LABEL_BYTE)
            {
                /* Preserve the legacy listing when label bits name an invalid target. */
                word &= 0x00ffffffUL;
                if (word && word <= words)
                {
                    write_numbers(&out, "$L_%04x ", word, 0);
                    continue;
                }
            }
            write_numbers(&out, "%d /*0x%x*/ ", word, word);
        }
        write_ascii(&out, "", 1);
    }
    write_ascii(&out, "", 1);
    return MM_SCRIPT_DISASSEMBLED;
}
