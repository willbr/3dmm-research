/* Block ownership policy converted from Kauai file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/block_owner.h"
#include <string.h>
#define OK THREE_MM_DATA_OK
#define BAD THREE_MM_DATA_ARGUMENT
static int services_valid(const ThreeMMHandleServices *h, const ThreeMMBlockFileServices *f)
{
    return h && h->allocate && h->release && h->size && h->lock && h->unlock && h->resize && f && f->retain &&
           f->release && f->create_temp && f->read && f->write;
}
int three_mm_owned_block_check(const ThreeMMOwnedBlock *b)
{
    if (!b || !services_valid(b->handles, b->files) || ((b->view.kind == THREE_MM_BLOCK_FILE) != (b->file != NULL)) ||
        (b->view.kind != THREE_MM_BLOCK_MEMORY && b->view.memory))
        return BAD;
    return three_mm_block_check(&b->view, 0, b->view.length, 1, NULL);
}
int three_mm_owned_block_init(ThreeMMOwnedBlock *b, const ThreeMMHandleServices *h, const ThreeMMBlockFileServices *f)
{
    if (!b || !services_valid(h, f))
        return BAD;
    memset(b, 0, sizeof(*b));
    b->handles = h;
    b->files = f;
    return OK;
}
int three_mm_owned_block_clear(ThreeMMOwnedBlock *b)
{
    void *file, *handle;
    if (three_mm_owned_block_check(b) != OK)
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
int three_mm_owned_block_set_file(ThreeMMOwnedBlock *b, const ThreeMMBlockFile *file, int packed)
{
    ThreeMMBlockView view;
    if (!file || !file->file || three_mm_owned_block_check(b) != OK)
        return BAD;
    memset(&view, 0, sizeof(view));
    view.kind = THREE_MM_BLOCK_FILE;
    view.start = file->offset;
    view.length = file->length;
    view.packed = packed != 0;
    if (three_mm_block_check(&view, 0, view.length, 1, NULL) != OK)
        return BAD;
    three_mm_owned_block_clear(b);
    b->files->retain(b->files->context, file->file);
    b->file = file->file;
    b->view = view;
    return OK;
}
int three_mm_owned_block_set_handle(ThreeMMOwnedBlock *b, void **handle, int packed)
{
    ThreeMMU32 bytes;
    void *owned;
    if (!handle || !*handle || three_mm_owned_block_check(b) != OK)
        return BAD;
    owned = *handle;
    bytes = b->handles->size(b->handles->context, owned);
    if (bytes >= THREE_MM_STORAGE_LIMIT)
        return BAD;
    three_mm_owned_block_clear(b);
    b->view.kind = THREE_MM_BLOCK_MEMORY;
    b->view.packed = packed != 0;
    b->view.memory = owned;
    b->view.length = b->view.capacity = bytes;
    *handle = NULL;
    return OK;
}
int three_mm_owned_block_set_temp(ThreeMMOwnedBlock *b, ThreeMMU32 bytes, int force_file)
{
    void *handle = NULL, *file;
    int packed;
    if (three_mm_owned_block_check(b) != OK || bytes >= THREE_MM_STORAGE_LIMIT)
        return BAD;
    packed = b->view.packed;
    if (!force_file && bytes < 0x00800000UL && b->handles->allocate(b->handles->context, bytes, &handle))
        return three_mm_owned_block_set_handle(b, &handle, packed);
    file = b->files->create_temp(b->files->context);
    if (!file)
        return THREE_MM_DATA_MEMORY;
    three_mm_owned_block_clear(b);
    b->view.kind = THREE_MM_BLOCK_FILE;
    b->view.packed = packed;
    b->view.length = bytes;
    b->file = file;
    return OK;
}
static int read_file(void *context, ThreeMMU32 offset, void *output, size_t n)
{
    ThreeMMOwnedBlock *b = (ThreeMMOwnedBlock *)context;
    return b->files->read(b->files->context, b->file, offset, output, n);
}
static int write_file(void *context, ThreeMMU32 offset, const void *input, size_t n)
{
    ThreeMMOwnedBlock *b = (ThreeMMOwnedBlock *)context;
    return b->files->write(b->files->context, b->file, offset, input, n);
}
int three_mm_owned_block_io(ThreeMMOwnedBlock *b, ThreeMMBlockIO *out)
{
    ThreeMMBlockIO io;
    if (!out || three_mm_owned_block_check(b) != OK)
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
int three_mm_owned_block_materialize(ThreeMMOwnedBlock *b, int packed_ok, ThreeMMBlockFile *out)
{
    ThreeMMBlockFile file;
    ThreeMMOwnedBlock target;
    ThreeMMBlockIO source, destination;
    int result;
    if (!out || three_mm_owned_block_check(b) != OK)
        return BAD;
    if (b->view.packed && !packed_ok)
        return THREE_MM_DATA_PACKED;
    memset(&file, 0, sizeof(file));
    if (b->view.kind == THREE_MM_BLOCK_FILE)
    {
        file.file = b->file;
        file.offset = b->view.start;
        file.length = b->view.length;
        b->files->retain(b->files->context, file.file);
        *out = file;
        return OK;
    }
    if (b->view.kind == THREE_MM_BLOCK_MEMORY && b->view.length)
    {
        file.file = b->files->create_temp(b->files->context);
        if (!file.file)
            return THREE_MM_DATA_MEMORY;
        file.length = b->view.length;
        three_mm_owned_block_init(&target, b->handles, b->files);
        target.file = file.file;
        target.view.kind = THREE_MM_BLOCK_FILE;
        target.view.length = file.length;
        three_mm_owned_block_io(b, &source);
        three_mm_owned_block_io(&target, &destination);
        result = three_mm_block_copy(&source, &destination, 1, NULL);
        if (result != OK)
        {
            three_mm_owned_block_clear(&target);
            return result;
        }
        /* Transfer the creation reference to the caller, leaving b untouched. */
    }
    *out = file;
    return OK;
}
int three_mm_owned_block_take_handle(ThreeMMOwnedBlock *b, int packed_ok, void **out, int *shrink_failed)
{
    ThreeMMBlockIO io;
    int result;
    if (three_mm_owned_block_io(b, &io) != OK)
        return BAD;
    result = three_mm_block_take_handle(&io, packed_ok, out, shrink_failed);
    if (result == OK)
        b->view = io.view;
    return result;
}
typedef struct CodeContext
{
    ThreeMMOwnedBlock *owner;
    void *context;
    ThreeMMBlockTransform code;
} CodeContext;
static int read_handle(void *context, void **handle)
{
    CodeContext *call = (CodeContext *)context;
    ThreeMMBlockIO io;
    three_mm_owned_block_io(call->owner, &io);
    return three_mm_block_read_handle(&io, 0, io.view.length, 1, handle) == OK;
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
static ThreeMMU32 handle_size(void *context, void *handle)
{
    CodeContext *call = (CodeContext *)context;
    return call->owner->handles->size(call->owner->handles->context, handle);
}
int three_mm_owned_block_code(ThreeMMOwnedBlock *b, int pack, void *context, ThreeMMBlockTransform code,
                              int *shrink_failed)
{
    CodeContext call;
    ThreeMMBlockDecoder codec;
    int result;
    void *prior_file;
    if (three_mm_owned_block_check(b) != OK || !code)
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
    result = pack ? three_mm_block_pack(&b->view, &codec, b->handles, shrink_failed)
                  : three_mm_block_unpack(&b->view, &codec);
    if (prior_file && b->view.kind == THREE_MM_BLOCK_MEMORY)
    {
        b->file = NULL;
        b->files->release(b->files->context, prior_file);
    }
    return result;
}
