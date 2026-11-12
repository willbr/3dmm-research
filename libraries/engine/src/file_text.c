/* File-text policy converted from Kauai file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/file_text.h"
#include <string.h>
#define OK THREE_MM_DATA_OK
#define BAD THREE_MM_DATA_ARGUMENT
#define LIMIT THREE_MM_STORAGE_LIMIT
static unsigned width(int encoding)
{
    if (encoding == THREE_MM_TEXT_MAC || encoding == THREE_MM_TEXT_WINDOWS)
        return 1;
    if (encoding == THREE_MM_TEXT_UTF16_BE || encoding == THREE_MM_TEXT_UTF16_LE)
        return 2;
    return 0;
}
int three_mm_file_text_translate(ThreeMMBlockFile *file, const ThreeMMBlockFileServices *files,
                                 const ThreeMMFileTextConverter *text, const ThreeMMStorageAllocator *allocator,
                                 int requested)
{
    union {
        ThreeMMU32 align;
        unsigned char bytes[512];
    } input_stack;
    union {
        ThreeMMU32 align;
        unsigned char bytes[1024];
    } output_stack;
    unsigned char header[2];
    unsigned char *input = input_stack.bytes, *output = output_stack.bytes;
    ThreeMMCollectionStorage scratch;
    ThreeMMU32 chunk = sizeof(input_stack.bytes), capacity, position = 0, written = 0, n, bytes;
    ThreeMMI32 count;
    unsigned signature;
    int inferred, result = OK;
    void *temporary, *prior;
    if (!file || !file->file || file->offset >= LIMIT || file->length >= LIMIT ||
        file->length > LIMIT - 1 - file->offset || !files || !files->read || !files->write || !files->create_temp ||
        !files->release || !text || !text->convert || !width(text->current_encoding) ||
        width(text->default_byte_encoding) != 1 || width(text->native_unicode_encoding) != 2 ||
        text->output_unit != width(text->current_encoding) || (requested && !width(requested)))
        return BAD;
    inferred = text->default_byte_encoding;
    if (file->length >= 2 && !(file->length % 2))
    {
        if (!files->read(files->context, file->file, file->offset, header, 2))
            return THREE_MM_DATA_IO;
        signature =
            text->native_big_endian ? ((unsigned)header[0] << 8) | header[1] : header[0] | ((unsigned)header[1] << 8);
        if (signature == 0xfeff || signature == 0xfffe)
        {
            inferred = text->native_unicode_encoding;
            if (signature == 0xfffe)
                inferred = inferred == THREE_MM_TEXT_UTF16_BE ? THREE_MM_TEXT_UTF16_LE : THREE_MM_TEXT_UTF16_BE;
            file->offset += 2;
            file->length -= 2;
        }
    }
    if (requested != inferred && (!requested || width(requested) < width(inferred) ||
                                  (width(requested) == width(inferred) && width(requested) == 2)))
        requested = inferred;
    if (requested == text->current_encoding)
        return OK;
    temporary = files->create_temp(files->context);
    if (!temporary)
        return THREE_MM_DATA_MEMORY;
    if (three_mm_storage_init(&scratch, allocator) != THREE_MM_STORAGE_OK)
    {
        files->release(files->context, temporary);
        return BAD;
    }
    if (file->length > sizeof(input_stack.bytes) &&
        three_mm_storage_resize_exact(&scratch, 0, file->length, THREE_MM_STORAGE_SPEED) == THREE_MM_STORAGE_OK)
    {
        input = scratch.buffers[0].data;
        chunk = file->length;
    }
    capacity = sizeof(output_stack.bytes) / text->output_unit;
    while (position < file->length)
    {
        n = file->length - position;
        if (n > chunk)
            n = chunk;
        if (!files->read(files->context, file->file, file->offset + position, input, n))
        {
            result = THREE_MM_DATA_IO;
            goto done;
        }
        position += n;
        count = text->convert(text->context, input, n, requested, NULL, 0);
        if (count <= 0)
            continue;
        if ((ThreeMMU32)count > (LIMIT - 1) / text->output_unit)
        {
            result = BAD;
            goto done;
        }
        bytes = (ThreeMMU32)count * text->output_unit;
        if ((ThreeMMU32)count > capacity)
        {
            /* Retain release-before-exact-allocation output growth. */
            three_mm_storage_resize_exact(&scratch, 1, 0, THREE_MM_STORAGE_NORMAL);
            if (three_mm_storage_resize_exact(&scratch, 1, bytes, THREE_MM_STORAGE_NORMAL) != THREE_MM_STORAGE_OK)
            {
                result = THREE_MM_DATA_MEMORY;
                goto done;
            }
            output = scratch.buffers[1].data;
            capacity = (ThreeMMU32)count;
        }
        if (text->convert(text->context, input, n, requested, output, (ThreeMMU32)count) != count)
        {
            result = THREE_MM_DATA_WRONG_SIZE;
            goto done;
        }
        if (bytes > LIMIT - 1 - written)
        {
            result = BAD;
            goto done;
        }
        if (!files->write(files->context, temporary, written, output, bytes))
        {
            result = THREE_MM_DATA_IO;
            goto done;
        }
        written += bytes;
    }
    prior = file->file;
    file->file = temporary;
    file->offset = 0;
    file->length = written;
    temporary = NULL;
    files->release(files->context, prior);
done:
    three_mm_storage_dispose(&scratch);
    if (temporary)
        files->release(files->context, temporary);
    return result;
}
