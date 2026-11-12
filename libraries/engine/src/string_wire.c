/* Serialized strings converted from Kauai utilstr.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/string_wire.h"
#include <string.h>
static unsigned width(int encoding)
{
    if (encoding == THREE_MM_TEXT_MAC || encoding == THREE_MM_TEXT_WINDOWS)
        return 1;
    if (encoding == THREE_MM_TEXT_UTF16_BE || encoding == THREE_MM_TEXT_UTF16_LE)
        return 2;
    return 0;
}
static unsigned word(const unsigned char *p, int big)
{
    return big ? ((unsigned)p[0] << 8) | p[1] : p[0] | ((unsigned)p[1] << 8);
}
static int valid(const ThreeMMStringWire *text)
{
    return text && text->buffer && width(text->current_encoding) == text->unit && (text->unit == 1 || text->unit == 2);
}
static unsigned unit(const ThreeMMStringWire *text, unsigned position)
{
    const unsigned char *p = (const unsigned char *)text->buffer + position * text->unit;
    return text->unit == 1 ? p[0] : word(p, text->native_big_endian);
}
static void put_unit(const ThreeMMStringWire *text, unsigned position, unsigned value)
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
ThreeMMU32 three_mm_string_wire_size(const ThreeMMStringWire *text)
{
    unsigned count;
    if (!valid(text))
        return 0;
    count = unit(text, 0);
    return count <= THREE_MM_STRING_MAX ? 2 + (count + 2) * text->unit : 0;
}
int three_mm_string_wire_get(const ThreeMMStringWire *text, void *output, ThreeMMU32 capacity)
{
    unsigned char *p = (unsigned char *)output;
    ThreeMMU32 bytes = three_mm_string_wire_size(text);
    if (!bytes || !output || capacity < bytes)
        return 0;
    p[text->native_big_endian ? 1 : 0] = (unsigned char)text->current_encoding;
    p[text->native_big_endian ? 0 : 1] = (unsigned char)((unsigned)text->current_encoding >> 8);
    memcpy(p + 2, text->buffer, bytes - 2);
    return 1;
}
int three_mm_string_wire_write(const ThreeMMStringWire *text, const ThreeMMStringWriter *writer, ThreeMMU32 offset)
{
    unsigned char header[2];
    ThreeMMU32 bytes = three_mm_string_wire_size(text);
    if (!bytes || !writer || !writer->write_at || offset > writer->length || bytes > writer->length - offset)
        return 0;
    header[text->native_big_endian ? 1 : 0] = (unsigned char)text->current_encoding;
    header[text->native_big_endian ? 0 : 1] = (unsigned char)((unsigned)text->current_encoding >> 8);
    return writer->write_at(writer->context, offset, header, 2) &&
           writer->write_at(writer->context, offset + 2, text->buffer, bytes - 2);
}
int three_mm_string_wire_read(const ThreeMMStringWire *text, const ThreeMMReadStream *stream, ThreeMMU32 offset,
                              ThreeMMU32 *end)
{
    union {
        unsigned short aligned;
        unsigned char bytes[512];
    } temporary;
    unsigned char header[2], prefix[2];
    unsigned source_width, count, i;
    int encoding;
    ThreeMMU32 position = offset, body;
    ThreeMMI32 translated;
    if (!valid(text))
        return 0;
    if (!stream || position > stream->length || stream->length - position < 2 ||
        !three_mm_read_exact(stream, position, header, 2))
        goto fail;
    position += 2;
    encoding = (int)word(header, text->native_big_endian);
    if (encoding == text->current_encoding)
    {
        if (stream->length - position < text->unit || !three_mm_read_exact(stream, position, text->buffer, text->unit))
            goto fail;
        position += text->unit;
        count = unit(text, 0);
        if (count > THREE_MM_STRING_MAX)
            goto fail;
        body = (count + 1) * text->unit;
        if (body > stream->length - position ||
            !three_mm_read_exact(stream, position, (unsigned char *)text->buffer + text->unit, body))
            goto fail;
        position += body;
        if (unit(text, count + 1))
            goto fail;
    }
    else
    {
        source_width = width(encoding);
        if (!source_width || !text->convert || stream->length - position < 2 * source_width ||
            !three_mm_read_exact(stream, position, prefix, source_width))
            goto fail;
        position += source_width;
        count = source_width == 1 ? prefix[0] : word(prefix, text->native_big_endian);
        if (source_width == 2 && encoding == text->swapped_unicode_encoding)
            count = ((count & 255U) << 8) | (count >> 8);
        if (count > THREE_MM_STRING_MAX)
            goto fail;
        body = (count + 1) * source_width;
        if (body > stream->length - position || !three_mm_read_exact(stream, position, temporary.bytes, body))
            goto fail;
        position += body;
        if (temporary.bytes[count * source_width] || (source_width == 2 && temporary.bytes[count * 2 + 1]))
            goto fail;
        translated = text->convert(text->context, temporary.bytes, count * source_width, encoding,
                                   (unsigned char *)text->buffer + text->unit, THREE_MM_STRING_MAX);
        if (translated < 0 || translated > THREE_MM_STRING_MAX || (count && !translated))
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
