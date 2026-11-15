/* Block ownership policy converted from Kauai file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/block_owner.h"
#include <string.h>
#define OK MM_DATA_OK
#define BAD MM_DATA_ARGUMENT
static int services_valid(const MMHandleServices *h, const MMBlockFileServices *f)
{
    return h && h->allocate && h->release && h->size && h->lock && h->unlock && h->resize && f && f->retain &&
           f->release && f->create_temp && f->read && f->write;
}
int mm_owned_block_check(const MMOwnedBlock *b)
{
    if (!b || !services_valid(b->handles, b->files) || ((b->view.kind == MM_BLOCK_FILE) != (b->file != NULL)) ||
        (b->view.kind != MM_BLOCK_MEMORY && b->view.memory))
        return BAD;
    return mm_block_check(&b->view, 0, b->view.length, 1, NULL);
}
int mm_owned_block_init(MMOwnedBlock *b, const MMHandleServices *h, const MMBlockFileServices *f)
{
    if (!b || !services_valid(h, f))
        return BAD;
    memset(b, 0, sizeof(*b));
    b->handles = h;
    b->files = f;
    return OK;
}
int mm_owned_block_clear(MMOwnedBlock *b)
{
    void *file, *handle;
    if (mm_owned_block_check(b) != OK)
        return BAD;
    file = b->file;
    handle = b->view.memory;
    b->file = NULL;
    memset(&b->view, 0, sizeof(b->view));
    if (file)
        b->files->release(b->files->context, file);
    if (handle)
        b->handles->release(b->handles->context, handle);
    return OK;
}
int mm_owned_block_set_file(MMOwnedBlock *b, const MMBlockFile *file, int packed)
{
    MMBlockView view;
    if (!file || !file->file || mm_owned_block_check(b) != OK)
        return BAD;
    memset(&view, 0, sizeof(view));
    view.kind = MM_BLOCK_FILE;
    view.start = file->offset;
    view.length = file->length;
    view.packed = packed != 0;
    if (mm_block_check(&view, 0, view.length, 1, NULL) != OK)
        return BAD;
    mm_owned_block_clear(b);
    b->files->retain(b->files->context, file->file);
    b->file = file->file;
    b->view = view;
    return OK;
}
int mm_owned_block_set_handle(MMOwnedBlock *b, void **handle, int packed)
{
    MMU32 bytes;
    void *owned;
    if (!handle || !*handle || mm_owned_block_check(b) != OK)
        return BAD;
    owned = *handle;
    bytes = b->handles->size(b->handles->context, owned);
    if (bytes >= MM_STORAGE_LIMIT)
        return BAD;
    mm_owned_block_clear(b);
    b->view.kind = MM_BLOCK_MEMORY;
    b->view.packed = packed != 0;
    b->view.memory = owned;
    b->view.length = b->view.capacity = bytes;
    *handle = NULL;
    return OK;
}
int mm_owned_block_set_temp(MMOwnedBlock *b, MMU32 bytes, int force_file)
{
    void *handle = NULL, *file;
    int packed;
    if (mm_owned_block_check(b) != OK || bytes >= MM_STORAGE_LIMIT)
        return BAD;
    packed = b->view.packed;
    if (!force_file && bytes < 0x00800000UL && b->handles->allocate(b->handles->context, bytes, &handle))
        return mm_owned_block_set_handle(b, &handle, packed);
    file = b->files->create_temp(b->files->context);
    if (!file)
        return MM_DATA_MEMORY;
    mm_owned_block_clear(b);
    b->view.kind = MM_BLOCK_FILE;
    b->view.packed = packed;
    b->view.length = bytes;
    b->file = file;
    return OK;
}
static int read_file(void *context, MMU32 offset, void *output, size_t n)
{
    MMOwnedBlock *b = (MMOwnedBlock *)context;
    return b->files->read(b->files->context, b->file, offset, output, n);
}
static int write_file(void *context, MMU32 offset, const void *input, size_t n)
{
    MMOwnedBlock *b = (MMOwnedBlock *)context;
    return b->files->write(b->files->context, b->file, offset, input, n);
}
int mm_owned_block_io(MMOwnedBlock *b, MMBlockIO *out)
{
    MMBlockIO io;
    if (!out || mm_owned_block_check(b) != OK)
        return BAD;
    memset(&io, 0, sizeof(io));
    io.view = b->view;
    io.handles = b->handles;
    io.context = b;
    io.read_file = read_file;
    io.write_file = write_file;
    *out = io;
    return OK;
}
int mm_owned_block_materialize(MMOwnedBlock *b, int packed_ok, MMBlockFile *out)
{
    MMBlockFile file;
    MMOwnedBlock target;
    MMBlockIO source, destination;
    int result;
    if (!out || mm_owned_block_check(b) != OK)
        return BAD;
    if (b->view.packed && !packed_ok)
        return MM_DATA_PACKED;
    memset(&file, 0, sizeof(file));
    if (b->view.kind == MM_BLOCK_FILE)
    {
        file.file = b->file;
        file.offset = b->view.start;
        file.length = b->view.length;
        b->files->retain(b->files->context, file.file);
        *out = file;
        return OK;
    }
    if (b->view.kind == MM_BLOCK_MEMORY && b->view.length)
    {
        file.file = b->files->create_temp(b->files->context);
        if (!file.file)
            return MM_DATA_MEMORY;
        file.length = b->view.length;
        mm_owned_block_init(&target, b->handles, b->files);
        target.file = file.file;
        target.view.kind = MM_BLOCK_FILE;
        target.view.length = file.length;
        mm_owned_block_io(b, &source);
        mm_owned_block_io(&target, &destination);
        result = mm_block_copy(&source, &destination, 1, NULL);
        if (result != OK)
        {
            mm_owned_block_clear(&target);
            return result;
        }
        /* Transfer the creation reference to the caller, leaving b untouched. */
    }
    *out = file;
    return OK;
}
int mm_owned_block_take_handle(MMOwnedBlock *b, int packed_ok, void **out, int *shrink_failed)
{
    MMBlockIO io;
    int result;
    if (mm_owned_block_io(b, &io) != OK)
        return BAD;
    result = mm_block_take_handle(&io, packed_ok, out, shrink_failed);
    if (result == OK)
        b->view = io.view;
    return result;
}
typedef struct CodeContext
{
    MMOwnedBlock *owner;
    void *context;
    MMBlockTransform code;
} CodeContext;
static int read_handle(void *context, void **handle)
{
    CodeContext *call = (CodeContext *)context;
    MMBlockIO io;
    mm_owned_block_io(call->owner, &io);
    return mm_block_read_handle(&io, 0, io.view.length, 1, handle) == OK;
}
static int transform(void *context, void **handle)
{
    CodeContext *call = (CodeContext *)context;
    return call->code(call->context, handle);
}
static void release_handle(void *context, void *handle)
{
    CodeContext *call = (CodeContext *)context;
    call->owner->handles->release(call->owner->handles->context, handle);
}
static MMU32 handle_size(void *context, void *handle)
{
    CodeContext *call = (CodeContext *)context;
    return call->owner->handles->size(call->owner->handles->context, handle);
}
int mm_owned_block_code(MMOwnedBlock *b, int pack, void *context, MMBlockTransform code,
                              int *shrink_failed)
{
    CodeContext call;
    MMBlockDecoder codec;
    int result;
    void *prior_file;
    if (mm_owned_block_check(b) != OK || !code)
        return BAD;
    call.owner = b;
    call.context = context;
    call.code = code;
    codec.context = &call;
    codec.read_file = read_handle;
    codec.decode = transform;
    codec.release = release_handle;
    codec.size = handle_size;
    prior_file = b->file;
    if (shrink_failed)
        *shrink_failed = 0;
    result = pack ? mm_block_pack(&b->view, &codec, b->handles, shrink_failed)
                  : mm_block_unpack(&b->view, &codec);
    if (prior_file && b->view.kind == MM_BLOCK_MEMORY)
    {
        b->file = NULL;
        b->files->release(b->files->context, prior_file);
    }
    return result;
}
