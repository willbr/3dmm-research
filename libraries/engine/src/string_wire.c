/* Serialized strings converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/string_wire.h"
#include "mm/string_ops.h"
#include <string.h>

int mm_string_text_utf8(const MMTextHost *host, const void *text, size_t count, unsigned unit,
                        char *out, size_t capacity, size_t *written)
{
    const unsigned short order = 1;
    unsigned short units[MM_STRING_MAX];
    size_t decoded;
    int encoding;
    if ((unit != 1 && unit != 2) || count > MM_STRING_MAX)
        return 0;
    encoding = unit == 1 ? MM_TEXT_WINDOWS :
               (*(const unsigned char *)&order ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE);
    decoded = mm_text_utf16(host, text, count * unit, encoding, units, MM_STRING_MAX);
    if (count && !decoded)
        return 0;
    return mm_text_write_utf8(units, decoded, out, capacity, written) == 1 ? 1 : -1;
}

static unsigned width(int encoding)
{
    if (encoding == MM_TEXT_MAC || encoding == MM_TEXT_WINDOWS)
        return 1;
    if (encoding == MM_TEXT_UTF16_BE || encoding == MM_TEXT_UTF16_LE)
        return 2;
    return 0;
}

int mm_string_wire_encode_ansi(const MMTextHost *host, int encoding, const char *name, void *out, MMU32 capacity,
                               MMU32 *written)
{
    const unsigned short order = 1;
    unsigned short characters[MM_STRING_MAX + 2];
    MMStringValue value = {0};
    MMStringWire wire = {0};
    value.unit = wire.unit = width(encoding);
    if (!wire.unit || !name || !written)
        return 0;
    value.buffer = wire.buffer = characters;
    wire.current_encoding = encoding;
    wire.native_big_endian = *(const unsigned char *)&order == 0;
    mm_string_from_ansi(&value, host, name, strlen(name));
    if (!mm_string_wire_get(&wire, out, capacity))
        return 0;
    *written = mm_string_wire_size(&wire);
    return 1;
}
static unsigned word(const unsigned char *p, int big)
{
    return big ? ((unsigned)p[0] << 8) | p[1] : p[0] | ((unsigned)p[1] << 8);
}
static int valid(const MMStringWire *text)
{
    return text && text->buffer && width(text->current_encoding) == text->unit && (text->unit == 1 || text->unit == 2);
}
static unsigned unit(const MMStringWire *text, unsigned position)
{
    const unsigned char *p = (const unsigned char *)text->buffer + position * text->unit;
    return text->unit == 1 ? p[0] : word(p, text->native_big_endian);
}
static void put_unit(const MMStringWire *text, unsigned position, unsigned value)
{
    unsigned char *p = (unsigned char *)text->buffer + position * text->unit;
    if (text->unit == 1)
        p[0] = (unsigned char)value;
    else
    {
        p[text->native_big_endian ? 1 : 0] = (unsigned char)value;
        p[text->native_big_endian ? 0 : 1] = (unsigned char)(value >> 8);
    }
}
MMU32 mm_string_wire_size(const MMStringWire *text)
{
    unsigned count;
    if (!valid(text))
        return 0;
    count = unit(text, 0);
    return count <= MM_STRING_MAX ? 2 + (count + 2) * text->unit : 0;
}
int mm_string_wire_get(const MMStringWire *text, void *output, MMU32 capacity)
{
    unsigned char *p = (unsigned char *)output;
    MMU32 bytes = mm_string_wire_size(text);
    if (!bytes || !output || capacity < bytes)
        return 0;
    p[text->native_big_endian ? 1 : 0] = (unsigned char)text->current_encoding;
    p[text->native_big_endian ? 0 : 1] = (unsigned char)((unsigned)text->current_encoding >> 8);
    memcpy(p + 2, text->buffer, bytes - 2);
    return 1;
}
int mm_string_wire_write(const MMStringWire *text, const MMStringWriter *writer, MMU32 offset)
{
    unsigned char header[2];
    MMU32 bytes = mm_string_wire_size(text);
    if (!bytes || !writer || !writer->write_at || offset > writer->length || bytes > writer->length - offset)
        return 0;
    header[text->native_big_endian ? 1 : 0] = (unsigned char)text->current_encoding;
    header[text->native_big_endian ? 0 : 1] = (unsigned char)((unsigned)text->current_encoding >> 8);
    return writer->write_at(writer->context, offset, header, 2) &&
           writer->write_at(writer->context, offset + 2, text->buffer, bytes - 2);
}
int mm_string_wire_read(const MMStringWire *text, const MMReadStream *stream, MMU32 offset,
                              MMU32 *end)
{
    union {
        unsigned short aligned;
        unsigned char bytes[512];
    } temporary;
    unsigned char header[2], prefix[2];
    unsigned source_width, count, i;
    int encoding;
    MMU32 position = offset, body;
    MMI32 translated;
    if (!valid(text))
        return 0;
    if (!stream || position > stream->length || stream->length - position < 2 ||
        !mm_read_exact(stream, position, header, 2))
        goto fail;
    position += 2;
    encoding = (int)word(header, text->native_big_endian);
    if (encoding == text->current_encoding)
    {
        if (stream->length - position < text->unit || !mm_read_exact(stream, position, text->buffer, text->unit))
            goto fail;
        position += text->unit;
        count = unit(text, 0);
        if (count > MM_STRING_MAX)
            goto fail;
        body = (count + 1) * text->unit;
        if (body > stream->length - position ||
            !mm_read_exact(stream, position, (unsigned char *)text->buffer + text->unit, body))
            goto fail;
        position += body;
        if (unit(text, count + 1))
            goto fail;
    }
    else
    {
        source_width = width(encoding);
        if (!source_width || !text->convert || stream->length - position < 2 * source_width ||
            !mm_read_exact(stream, position, prefix, source_width))
            goto fail;
        position += source_width;
        count = source_width == 1 ? prefix[0] : word(prefix, text->native_big_endian);
        if (source_width == 2 && encoding == text->swapped_unicode_encoding)
            count = ((count & 255U) << 8) | (count >> 8);
        if (count > MM_STRING_MAX)
            goto fail;
        body = (count + 1) * source_width;
        if (body > stream->length - position || !mm_read_exact(stream, position, temporary.bytes, body))
            goto fail;
        position += body;
        if (temporary.bytes[count * source_width] || (source_width == 2 && temporary.bytes[count * 2 + 1]))
            goto fail;
        translated = text->convert(text->context, temporary.bytes, count * source_width, encoding,
                                   (unsigned char *)text->buffer + text->unit, MM_STRING_MAX);
        if (translated < 0 || translated > MM_STRING_MAX || (count && !translated))
            goto fail;
        count = (unsigned)translated;
        put_unit(text, 0, count);
        put_unit(text, count + 1, 0);
    }
    for (i = 1; i <= count; ++i)
        if (!unit(text, i))
            goto fail;
    if (!end && position != stream->length)
        goto fail;
    if (end)
        *end = position;
    return 1;
fail:
    put_unit(text, 0, 0);
    put_unit(text, 1, 0);
    return 0;
}
