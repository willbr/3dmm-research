/* Block/handle policy converted from Kauai file.cpp and codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/block_io.h"
#include <string.h>
#define OK MM_DATA_OK
#define BAD MM_DATA_ARGUMENT
#define LIMIT MM_STORAGE_LIMIT
static int handles_valid(const MMHandleServices *h)
{
    return h && h->allocate && h->release && h->size && h->lock && h->unlock && h->resize;
}
int mm_stream_copy(const MMReadStream *source, const MMStorageWriter *dest,
                         const MMStorageAllocator *a)
{
    unsigned char fallback[1024];
    unsigned char *buffer = fallback;
    MMCollectionStorage scratch;
    MMU32 offset = 0, chunk = sizeof(fallback), n;
    int result = OK;
    if (!source || !dest || !source->read_at || !dest->write_at || source->length >= LIMIT ||
        dest->length != source->length)
        return BAD;
    if (mm_storage_init(&scratch, a) != MM_STORAGE_OK)
        return BAD;
    if (source->length > sizeof(fallback) &&
        mm_storage_resize_exact(&scratch, 0, source->length, MM_STORAGE_SPEED) == MM_STORAGE_OK)
    {
        buffer = scratch.buffers[0].data;
        chunk = source->length;
    }
    while (offset < source->length)
    {
        n = source->length - offset;
        if (n > chunk)
            n = chunk;
        if (!source->read_at(source->context, offset, buffer, n) || !dest->write_at(dest->context, offset, buffer, n))
        {
            result = MM_DATA_IO;
            break;
        }
        offset += n;
    }
    mm_storage_dispose(&scratch);
    return result;
}
int mm_block_read(const MMBlockIO *b, MMU32 offset, void *output, MMU32 n, int packed_ok)
{
    MMU32 physical;
    unsigned char *p;
    int result;
    if (!b || (!output && n))
        return BAD;
    result = mm_block_check(&b->view, offset, n, packed_ok, &physical);
    if (result != OK)
        return result;
    if (b->view.kind == MM_BLOCK_FILE)
    {
        if (!b->read_file)
            return BAD;
        return !n || b->read_file(b->context, physical, output, n) ? OK : MM_DATA_IO;
    }
    if (b->view.kind == MM_BLOCK_MEMORY)
    {
        if (!handles_valid(b->handles))
            return BAD;
        p = (unsigned char *)b->handles->lock(b->handles->context, b->view.memory);
        if (n)
            memcpy(output, p + physical, n);
        b->handles->unlock(b->handles->context, b->view.memory);
    }
    return OK;
}
static int stream_read(void *context, MMU32 offset, void *output, size_t bytes)
{
    return (size_t)(MMU32)bytes == bytes &&
           mm_block_read((const MMBlockIO *)context, offset, output, (MMU32)bytes, 0) == OK;
}
int mm_block_io_stream(MMBlockIO *io, MMReadStream *out)
{
    int result;
    if (!io || !out)
        return BAD;
    result = mm_block_check(&io->view, 0, io->view.length, 0, NULL);
    if (result != OK)
        return result;
    out->context = io;
    out->length = io->view.length;
    out->read_at = stream_read;
    return OK;
}
int mm_block_write(const MMBlockIO *b, MMU32 offset, const void *input, MMU32 n, int packed_ok)
{
    MMU32 physical;
    unsigned char *p;
    int result;
    if (!b || (!input && n))
        return BAD;
    result = mm_block_check(&b->view, offset, n, packed_ok, &physical);
    if (result != OK)
        return result;
    if (b->view.kind == MM_BLOCK_FILE)
    {
        if (!b->write_file)
            return BAD;
        return !n || b->write_file(b->context, physical, input, n) ? OK : MM_DATA_IO;
    }
    if (b->view.kind == MM_BLOCK_MEMORY)
    {
        if (!handles_valid(b->handles))
            return BAD;
        p = (unsigned char *)b->handles->lock(b->handles->context, b->view.memory);
        if (n)
            memcpy(p + physical, input, n);
        b->handles->unlock(b->handles->context, b->view.memory);
    }
    return OK;
}
static int stream_write(void *context, MMU32 offset, const void *input, size_t bytes)
{
    return (size_t)(MMU32)bytes == bytes &&
           mm_block_write((const MMBlockIO *)context, offset, input, (MMU32)bytes, 0) == OK;
}
int mm_block_io_writer(MMBlockIO *io, MMStorageWriter *out)
{
    int result;
    if (!io || !out)
        return BAD;
    result = mm_block_check(&io->view, 0, io->view.length, 0, NULL);
    if (result != OK)
        return result;
    out->context = io;
    out->length = io->view.length;
    out->write_at = stream_write;
    return OK;
}
static int read_relative(void *context, MMU32 offset, void *p, size_t n)
{
    const MMBlockIO *b = (const MMBlockIO *)context;
    return b->read_file(b->context, b->view.start + offset, p, n);
}
static int write_relative(void *context, MMU32 offset, const void *p, size_t n)
{
    const MMBlockIO *b = (const MMBlockIO *)context;
    return b->write_file(b->context, b->view.start + offset, p, n);
}
int mm_block_copy(const MMBlockIO *source, const MMBlockIO *dest, int packed_ok,
                        const MMStorageAllocator *a)
{
    MMReadStream input;
    MMStorageWriter output;
    unsigned char *p;
    int result;
    if (!source || !dest)
        return BAD;
    result = mm_block_check(&source->view, 0, source->view.length, packed_ok, NULL);
    if (result != OK)
        return result;
    result = mm_block_check(&dest->view, 0, dest->view.length, 1, NULL);
    if (result != OK)
        return result;
    if (source->view.length != dest->view.length)
        return MM_DATA_WRONG_SIZE;
    if (dest->view.kind == MM_BLOCK_FILE)
    {
        if (!dest->write_file)
            return BAD;
        if (source->view.kind == MM_BLOCK_FILE)
        {
            if (!source->read_file)
                return BAD;
            input.context = (void *)source;
            input.length = source->view.length;
            input.read_at = read_relative;
            output.context = (void *)dest;
            output.length = dest->view.length;
            output.write_at = write_relative;
            return mm_stream_copy(&input, &output, a);
        }
        if (source->view.kind == MM_BLOCK_MEMORY)
        {
            if (!handles_valid(source->handles))
                return BAD;
            p = (unsigned char *)source->handles->lock(source->handles->context, source->view.memory);
            result = mm_block_write(dest, 0, p + source->view.start, source->view.length, 1);
            source->handles->unlock(source->handles->context, source->view.memory);
            return result;
        }
    }
    else if (dest->view.kind == MM_BLOCK_MEMORY)
    {
        if (!handles_valid(dest->handles))
            return BAD;
        p = (unsigned char *)dest->handles->lock(dest->handles->context, dest->view.memory);
        result = mm_block_read(source, 0, p + dest->view.start, source->view.length, 1);
        dest->handles->unlock(dest->handles->context, dest->view.memory);
        return result;
    }
    return OK;
}
int mm_block_read_handle(const MMBlockIO *b, MMU32 offset, MMU32 n, int packed_ok, void **out)
{
    const MMHandleServices *h;
    void *p, *handle = NULL;
    int result;
    if (!out)
        return BAD;
    *out = NULL;
    if (!b)
        return BAD;
    result = mm_block_check(&b->view, offset, n, packed_ok, NULL);
    if (result != OK)
        return result;
    if (b->view.kind == MM_BLOCK_EMPTY)
        return OK;
    h = b->handles;
    if (!handles_valid(h))
        return BAD;
    if (!h->allocate(h->context, n, &handle))
        return MM_DATA_MEMORY;
    p = h->lock(h->context, handle);
    result = mm_block_read(b, offset, p, n, 1);
    h->unlock(h->context, handle);
    if (result != OK)
        h->release(h->context, handle);
    else
        *out = handle;
    return result;
}
int mm_block_write_handle(const MMBlockIO *b, MMU32 offset, void *handle, int packed_ok)
{
    const MMHandleServices *h;
    MMU32 n;
    void *p;
    int result;
    if (!b || !handle || !handles_valid(b->handles))
        return BAD;
    h = b->handles;
    n = h->size(h->context, handle);
    result = mm_block_check(&b->view, offset, n, packed_ok, NULL);
    if (result != OK)
        return result;
    p = h->lock(h->context, handle);
    result = mm_block_write(b, offset, p, n, 1);
    h->unlock(h->context, handle);
    return result;
}
int mm_block_take_handle(MMBlockIO *b, int packed_ok, void **out, int *shrink_failed)
{
    MMU32 length, start, capacity;
    void *handle;
    unsigned char *p;
    const MMHandleServices *h;
    int result;
    if (shrink_failed)
        *shrink_failed = 0;
    if (!b || !out)
        return BAD;
    *out = NULL;
    result = mm_block_check(&b->view, 0, b->view.length, packed_ok, NULL);
    if (result != OK)
        return result;
    if (b->view.kind != MM_BLOCK_MEMORY)
        return mm_block_read_handle(b, 0, b->view.length, 1, out);
    h = b->handles;
    if (!handles_valid(h))
        return BAD;
    handle = b->view.memory;
    start = b->view.start;
    length = b->view.length;
    capacity = b->view.capacity;
    memset(&b->view, 0, sizeof(b->view));
    if (start && length)
    {
        p = (unsigned char *)h->lock(h->context, handle);
        memmove(p, p + start, length);
        h->unlock(h->context, handle);
    }
    if (capacity > length && !h->resize(h->context, &handle, length) && shrink_failed)
        *shrink_failed = 1;
    *out = handle;
    return OK;
}
int mm_handle_code(const MMHandleServices *h, void **owned, void *context, MMHandleCode code,
                         int *shrink_failed)
{
    void *source, *dest, *output = NULL;
    MMU32 bytes, capacity;
    MMI32 written = 0;
    int ok;
    if (shrink_failed)
        *shrink_failed = 0;
    if (!handles_valid(h) || !owned || !*owned || !code)
        return BAD;
    bytes = h->size(h->context, *owned);
    if (!bytes || bytes >= LIMIT)
        return BAD;
    source = h->lock(h->context, *owned);
    ok = code(context, source, bytes, NULL, 0, &written);
    h->unlock(h->context, *owned);
    if (!ok)
        return MM_DATA_CODEC;
    if (written < 0 || (MMU32)written >= LIMIT)
        return MM_DATA_WRONG_SIZE;
    capacity = (MMU32)written;
    if (!h->allocate(h->context, capacity, &output))
        return MM_DATA_MEMORY;
    source = h->lock(h->context, *owned);
    dest = h->lock(h->context, output);
    ok = code(context, source, bytes, dest, capacity, &written);
    h->unlock(h->context, output);
    h->unlock(h->context, *owned);
    if (!ok || written < 0 || (MMU32)written > capacity)
    {
        h->release(h->context, output);
        return ok ? MM_DATA_WRONG_SIZE : MM_DATA_CODEC;
    }
    if ((MMU32)written < capacity && !h->resize(h->context, &output, (MMU32)written) && shrink_failed)
        *shrink_failed = 1;
    h->release(h->context, *owned);
    *owned = output;
    return OK;
}
int mm_block_pack(MMBlockView *view, const MMBlockDecoder *code, const MMHandleServices *h,
                        int *shrink_failed)
{
    MMBlockIO io;
    void *handle;
    int result, failed = 0;
    if (shrink_failed)
        *shrink_failed = 0;
    if (!view)
        return BAD;
    result = mm_block_check(view, 0, view->length, 1, NULL);
    if (result != OK)
        return result;
    if (view->packed)
        return OK;
    if (view->kind == MM_BLOCK_EMPTY)
        return MM_DATA_CODEC;
    if (!code || !code->decode || !code->release || !code->size)
        return BAD;
    if (view->kind == MM_BLOCK_FILE)
    {
        if (!code->read_file)
            return BAD;
        handle = NULL;
        if (!code->read_file(code->context, &handle))
        {
            if (handle)
                code->release(code->context, handle);
            return MM_DATA_IO;
        }
        if (!code->decode(code->context, &handle))
        {
            code->release(code->context, handle);
            return MM_DATA_CODEC;
        }
    }
    else
    {
        if (view->start || view->length != view->capacity)
        {
            memset(&io, 0, sizeof(io));
            io.view = *view;
            io.handles = h;
            result = mm_block_take_handle(&io, 1, &handle, &failed);
            if (result != OK)
                return result;
            view->memory = handle;
            view->start = 0;
            view->length = view->capacity = code->size(code->context, handle);
            if (shrink_failed)
                *shrink_failed = failed;
        }
        if (!code->decode(code->context, &view->memory))
            return MM_DATA_CODEC;
        handle = view->memory;
    }
    view->kind = MM_BLOCK_MEMORY;
    view->memory = handle;
    view->start = 0;
    view->length = view->capacity = code->size(code->context, handle);
    view->packed = 1;
    return OK;
}
