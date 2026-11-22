/* Chunky source parsing converted from Kauai chcm.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunky_source.h"
#include "mm/string_ops.h"
#include "mm/string_wire.h"
#include "mm/wide.h"

static void report(const MMChunkySource *source, int error)
{
    source->report(source->context, error);
}

static void skip_header(const MMChunkySource *source, int error, MMChunkySourceToken *token)
{
    report(source, error);
    while (source->next(source->context, token, MM_CHUNKY_READ_CLEAN) && token->kind != ttCloseParen)
        ;
}

MMI32 mm_chunky_parse_header(const MMChunkySource *source, MMChunkyHeaderField *fields, MMU32 capacity)
{
    MMChunkySourceToken token;
    MMU32 i, tail;
    if (!source->next(source->context, &token, MM_CHUNKY_READ_RAW))
        return -1;
    if (token.kind != ttOpenParen)
    {
        skip_header(source, ertParenOpen, &token);
        return -1;
    }

    for (i = 0; i < capacity; ++i)
    {
        MMChunkyHeaderField *field = &fields[i];
        if (!source->next(source->context, &token, MM_CHUNKY_READ_CLEAN))
            return -1;
        switch (token.kind)
        {
        case ttCloseParen:
            for (tail = i; tail < capacity; ++tail)
                if (fields[tail].text)
                    source->set_text(fields[tail].text, NULL, 0);
            return (MMI32)i;
        case ttLong:
            if (field->text)
            {
                report(source, ertNeedString);
                source->set_text(field->text, NULL, 0);
            }
            else
                field->number = token.number;
            break;
        case ttString:
            if (field->text)
                source->set_text(field->text, token.text, token.length);
            else
            {
                report(source, ertNeedNumber);
                field->number = 0;
            }
            break;
        default:
            report(source, ertBadToken);
            break;
        }
    }

    if (!source->next(source->context, &token, MM_CHUNKY_READ_RAW))
        return -1;
    if (token.kind != ttCloseParen)
    {
        skip_header(source, ertParenClose, &token);
        return -1;
    }
    return (MMI32)capacity;
}

static void append(const MMChunkySource *source, const void *bytes, MMU32 size)
{
    if (!source->append(source->context, bytes, size))
        report(source, ertOom);
}

static void append_number(const MMChunkySource *source, MMI32 value, unsigned width)
{
    unsigned char bytes[4];
    int status = mm_chunky_write_number(width, source->format->byte_order != source->native_order, value, bytes);
    if (status == MM_CHUNKY_NUMBER_BYTE_RANGE)
        report(source, ertRangeByte);
    else if (status == MM_CHUNKY_NUMBER_SHORT_RANGE)
        report(source, ertRangeShort);
    else
        append(source, bytes, width);
}

static void append_string(const MMChunkySource *source, const MMChunkySourceToken *token)
{
    union
    {
        unsigned char narrow[MM_STRING_MAX + 2];
        unsigned short wide[MM_STRING_MAX + 2];
    } counted;
    const unsigned short one = 1;
    unsigned char serialized[2 + (MM_STRING_MAX + 2) * 2];
    unsigned unit = source->unit_bytes;
    unsigned char *bytes = (unsigned char *)&counted;
    MMU32 size;
    MMStringValue value;
    MMStringWire wire;

    if ((unit != 1 && unit != 2) || token->length > MM_STRING_MAX || (token->length && !token->text))
    {
        report(source, ertBadToken);
        return;
    }
    value.buffer = &counted;
    value.unit = unit;
    value.context = NULL;
    value.check_copy = NULL;
    mm_string_value_set(&value, token->text, (MMI32)token->length);

    switch (source->format->string_mode)
    {
    case smSt:
        append(source, bytes, (token->length + 1) * unit);
        break;
    case smStz:
        append(source, bytes, (token->length + 2) * unit);
        break;
    case smSz:
        bytes += unit;
        append(source, bytes, (MMU32)(mm_string_zero_length(bytes, unit) + 1) * unit);
        break;
    default: /* Serialized String uses native encoding, independent of OSK mode. */
        wire.buffer = &counted;
        wire.unit = unit;
        wire.current_encoding = source->native_encoding;
        wire.swapped_unicode_encoding = 0;
        wire.native_big_endian = *(const unsigned char *)&one == 0;
        wire.context = NULL;
        wire.convert = NULL;
        size = mm_string_wire_size(&wire);
        if (mm_string_wire_get(&wire, serialized, sizeof(serialized)))
            append(source, serialized, size);
        else
            report(source, ertBadToken);
        break;
    }
}

static void align_data(const MMChunkySource *source)
{
    static const unsigned char zeros[MM_CHUNKY_ALIGN_MAX - 1] = {0};
    MMChunkySourceToken token;
    MMU32 remainder;
    if (!source->next(source->context, &token, MM_CHUNKY_READ_CLEAN))
        return;
    if (token.kind != ttLong)
        report(source, ertNeedNumber);
    else if (token.number < MM_CHUNKY_ALIGN_MIN || token.number > MM_CHUNKY_ALIGN_MAX)
        report(source, ertBodyAlignRange);
    else
    {
        remainder = source->size(source->context) % (MMU32)token.number;
        if (remainder)
            append(source, zeros, (MMU32)token.number - remainder);
    }
}

int mm_chunky_parse_data(const MMChunkySource *source, MMChunkySourceToken *token)
{
    enum { EMPTY, NUMBER, AFTER_OR } pending = EMPTY;
    MMI32 value = 0;
    unsigned width = 0;
    int consumed = 0;
    for (;;)
    {
        if (token->kind == ttBOr)
        {
            if (pending == EMPTY)
                return consumed;
            if (pending != NUMBER)
                break;
            pending = AFTER_OR;
        }
        else if (token->kind == ttLong)
        {
            if (pending == AFTER_OR)
            {
                if (width != (unsigned)source->format->number_bytes)
                    break;
                value = mm_signed_word((MMU32)value | (MMU32)token->number);
            }
            else
            {
                /* Width is captured with the number. Order follows lookahead. */
                if (pending == NUMBER)
                    append_number(source, value, width);
                value = token->number;
                width = (unsigned)source->format->number_bytes;
            }
            pending = NUMBER;
        }
        else
        {
            if (pending == AFTER_OR)
                break;
            if (pending == NUMBER)
                append_number(source, value, width);
            pending = EMPTY;

            switch (token->kind)
            {
            case ttString: append_string(source, token); break;
            case ttAlign: align_data(source); break;
            case ttFile: source->import_file(source->context); break;
            case ttBo: append_number(source, source->native_order, 2); break;
            case ttOsk: append_number(source, source->format->os_kind, 2); break;
            default: return consumed;
            }
        }
        consumed = 1;
        if (!source->next(source->context, token, MM_CHUNKY_READ_CLEAN_EOF))
            break;
    }
    token->kind = ttError;
    return consumed;
}
