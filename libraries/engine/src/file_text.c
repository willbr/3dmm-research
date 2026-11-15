/* File-text policy converted from Kauai file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/file_text.h"
#include <string.h>
#define OK MM_DATA_OK
#define BAD MM_DATA_ARGUMENT
#define LIMIT MM_STORAGE_LIMIT
static unsigned width(int encoding)
{
    if (encoding == MM_TEXT_MAC || encoding == MM_TEXT_WINDOWS)
        return 1;
    if (encoding == MM_TEXT_UTF16_BE || encoding == MM_TEXT_UTF16_LE)
        return 2;
    return 0;
}
int mm_file_text_translate(MMBlockFile *file, const MMBlockFileServices *files,
                                 const MMFileTextConverter *text, const MMStorageAllocator *allocator,
                                 int requested)
{
    union {
        MMU32 align;
        unsigned char bytes[512];
    } input_stack;
    union {
        MMU32 align;
        unsigned char bytes[1024];
    } output_stack;
    unsigned char header[2];
    unsigned char *input = input_stack.bytes, *output = output_stack.bytes;
    MMCollectionStorage scratch;
    MMU32 chunk = sizeof(input_stack.bytes), capacity, position = 0, written = 0, n, bytes;
    MMI32 count;
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
            return MM_DATA_IO;
        signature =
            text->native_big_endian ? ((unsigned)header[0] << 8) | header[1] : header[0] | ((unsigned)header[1] << 8);
        if (signature == 0xfeff || signature == 0xfffe)
        {
            inferred = text->native_unicode_encoding;
            if (signature == 0xfffe)
                inferred = inferred == MM_TEXT_UTF16_BE ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE;
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
        return MM_DATA_MEMORY;
    if (mm_storage_init(&scratch, allocator) != MM_STORAGE_OK)
    {
        files->release(files->context, temporary);
        return BAD;
    }
    if (file->length > sizeof(input_stack.bytes) &&
        mm_storage_resize_exact(&scratch, 0, file->length, MM_STORAGE_SPEED) == MM_STORAGE_OK)
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
            result = MM_DATA_IO;
            goto done;
        }
        position += n;
        count = text->convert(text->context, input, n, requested, NULL, 0);
        if (count <= 0)
            continue;
        if ((MMU32)count > (LIMIT - 1) / text->output_unit)
        {
            result = BAD;
            goto done;
        }
        bytes = (MMU32)count * text->output_unit;
        if ((MMU32)count > capacity)
        {
            /* Retain release-before-exact-allocation output growth. */
            mm_storage_resize_exact(&scratch, 1, 0, MM_STORAGE_NORMAL);
            if (mm_storage_resize_exact(&scratch, 1, bytes, MM_STORAGE_NORMAL) != MM_STORAGE_OK)
            {
                result = MM_DATA_MEMORY;
                goto done;
            }
            output = scratch.buffers[1].data;
            capacity = (MMU32)count;
        }
        if (text->convert(text->context, input, n, requested, output, (MMU32)count) != count)
        {
            result = MM_DATA_WRONG_SIZE;
            goto done;
        }
        if (bytes > LIMIT - 1 - written)
        {
            result = BAD;
            goto done;
        }
        if (!files->write(files->context, temporary, written, output, bytes))
        {
            result = MM_DATA_IO;
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
    mm_storage_dispose(&scratch);
    if (temporary)
        files->release(files->context, temporary);
    return result;
}
