#include "mm/chunky_listing.h"
#include <string.h>

enum { row_bytes = 8, row_text_limit = 5 * row_bytes + 7 + row_bytes + 2 };

static size_t format_row(unsigned char *text, const unsigned char *bytes, size_t count)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t i;
    unsigned char value;

    memset(text, ' ', 5 * row_bytes);
    for (i = 0; i < count; ++i)
    {
        value = bytes[i];
        text[5 * i] = '0';
        text[5 * i + 1] = 'x';
        text[5 * i + 2] = digits[value >> 4];
        text[5 * i + 3] = digits[value & 15];
    }
    text += 5 * row_bytes;
    memcpy(text, "   // '", 7);
    text += 7;
    for (i = 0; i < count; ++i)
    {
        value = bytes[i];
        text[i] = value < 0x20 || value == 0x7F ? '?' : value;
    }
    memcpy(text + count, "' ", 2);
    return row_text_limit - row_bytes + count;
}

static void emit_line(const unsigned char *line, size_t length, MMChunkyWriteLine write_line, void *context)
{
    write_line(context, line, length > MM_CHUNKY_LISTING_LINE_LIMIT ? MM_CHUNKY_LISTING_LINE_LIMIT : length);
}

int mm_chunky_list_bytes(const MMReadStream *stream, size_t indent, MMChunkyWriteLine write_line, void *context)
{
    unsigned char line[MM_CHUNKY_LISTING_LINE_LIMIT + row_text_limit];
    unsigned char buffer[4096];
    MMU32 offset, remaining;
    size_t count, row, row_count, length;

    if (!stream || !write_line || indent > MM_CHUNKY_LISTING_LINE_LIMIT)
        return 0;
    memset(line, '\t', indent);
    memcpy(line + indent, "BYTE", 4);
    emit_line(line, indent + 4, write_line, context);
    for (offset = 0; offset < stream->length; offset += (MMU32)count)
    {
        remaining = stream->length - offset;
        count = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        if (!mm_read_exact(stream, offset, buffer, count))
            return 0;
        for (row = 0; row < count; row += row_count)
        {
            row_count = count - row < row_bytes ? count - row : row_bytes;
            length = format_row(line + indent, buffer + row, row_count);
            emit_line(line, indent + length, write_line, context);
        }
    }
    return 1;
}
