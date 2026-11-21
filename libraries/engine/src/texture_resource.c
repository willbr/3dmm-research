/* Texture map policy converted from bren/tmap.cpp. */
#include "mm/texture_resource.h"
#include <stdlib.h>
#include <string.h>
static MMU32 word(const unsigned char *p, unsigned n, int big)
{
    MMU32 v = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        v = (v << 8) | p[big ? i : n - 1 - i];
    return v;
}
static void put(unsigned char *p, unsigned n, MMU32 v, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
    {
        p[big ? n - 1 - i : i] = (unsigned char)v;
        v >>= 8;
    }
}
static MMI32 signed16(MMU32 n)
{
    return n < 32768U ? (MMI32)n : (MMI32)n - 65536L;
}
int mm_texture_payload(const MMTextureDescription *d, MMI32 *n)
{
    if (d->row_bytes < 0 || d->row_bytes > 32767 || d->width < 0 || d->width > 65535L || d->height < 0 ||
        d->height > 65535L || (d->row_bytes && (MMU32)d->height > 0x7fffffebUL / (MMU32)d->row_bytes))
        return 0;
    *n = d->row_bytes * d->height;
    return 1;
}
int mm_texture_header_read(const unsigned char *b, int big, MMTextureDescription *d, int *bad)
{
    MMI32 n;
    if (word(b, 2, big) != 1)
        big = !big;
    if (bad)
        *bad = word(b, 2, big) != 1;
    d->row_bytes = signed16(word(b + 4, 2, big));
    d->type = b[6];
    d->flags = b[7];
    d->base_x = word(b + 8, 2, big);
    d->base_y = word(b + 10, 2, big);
    d->width = word(b + 12, 2, big);
    d->height = word(b + 14, 2, big);
    d->origin_x = signed16(word(b + 16, 2, big));
    d->origin_y = signed16(word(b + 18, 2, big));
    return d->height <= 32767 && mm_texture_payload(d, &n);
}
void mm_texture_header_write(unsigned char *b, int big, MMU32 system, const MMTextureDescription *d)
{
    put(b, 2, 1, big);
    put(b + 2, 2, system, big);
    put(b + 4, 2, (MMU32)d->row_bytes, big);
    b[6] = (unsigned char)d->type;
    b[7] = (unsigned char)d->flags;
    put(b + 8, 2, (MMU32)d->base_x, big);
    put(b + 10, 2, (MMU32)d->base_y, big);
    put(b + 12, 2, (MMU32)d->width, big);
    put(b + 14, 2, (MMU32)d->height, big);
    put(b + 16, 2, (MMU32)d->origin_x, big);
    put(b + 18, 2, (MMU32)d->origin_y, big);
}
MMI32 mm_texture_size(const MMTextureResource *m)
{
    MMI32 n;
    return mm_texture_payload(&m->description, &n) ? 20 + n : -1;
}
static MMTextureOwner *create_texture(const MMTextureOwnerHost *host)
{
    const MMStorageAllocator *allocator = host->objects;
    MMU32 bytes = host->object_bytes;
    MMTextureOwner *owner;
    if (bytes < sizeof(*owner))
        return NULL;
    owner = allocator ? allocator->resize(allocator->context, NULL, 0, bytes, MM_STORAGE_NORMAL) : malloc(bytes);
    if (owner)
    {
        memset(owner, 0, bytes);
        mm_reference_init(&owner->references);
        owner->host = host;
    }
    return owner;
}

const MMTextureResource *mm_texture_owner_resource(MMTextureOwner *owner)
{
    const MMTextureServices *s = owner->host->services;
    if (s->sync)
        s->sync(s->context, owner, &owner->texture);
    return &owner->texture;
}

static MMResourceObject *texture_binding(void *object) { return &((MMTextureOwner *)object)->binding; }
static MMReference *texture_references(void *object) { return &((MMTextureOwner *)object)->references; }

static void destroy_texture(void *object)
{
    MMTextureOwner *owner = object;
    const MMTextureOwnerHost *host = owner->host;
    const MMTextureServices *s = host->services;
    const MMStorageAllocator *allocator = host->objects;
    MMU32 bytes = host->object_bytes;
    const MMTextureResource *texture = mm_texture_owner_resource(owner);
    if (texture->imported_owner)
        s->free_import(s->context, texture->imported_owner);
    else
        s->free_memory(s->context, &owner->texture.pixels);
    mm_resource_object_dispose(owner, mm_texture_owner_services());
    if (allocator)
        allocator->release(allocator->context, owner, bytes);
    else
        free(owner);
}

static void mark_texture(void *object)
{
    MMTextureOwner *owner = object;
    if (owner->host->mark_memory)
    {
        const MMTextureResource *texture = mm_texture_owner_resource(owner);
        owner->host->mark_memory(owner);
        if (!texture->imported_owner && texture->pixels)
            owner->host->mark_memory(texture->pixels);
    }
}

const MMResourceObjectServices *mm_texture_owner_services(void)
{
    static const MMResourceObjectServices services = {texture_binding, texture_references, destroy_texture,
                                                       NULL, mark_texture};
    return &services;
}

void mm_texture_owner_release(MMTextureOwner *owner)
{
    if (owner)
        mm_resource_object_release(owner, mm_texture_owner_services());
}

MMTextureOwner *mm_texture_new(void *pixels, MMI32 width, MMI32 height, const MMTextureOwnerHost *host)
{
    const MMTextureServices *s = host->services;
    MMTextureDescription d;
    MMI32 n;
    MMTextureOwner *owner;
    memset(&d, 0, sizeof(d));
    d.row_bytes = width;
    d.width = width;
    d.height = height;
    d.type = s->indexed_type;
    d.flags = s->linear_flag;
    if (!mm_texture_payload(&d, &n) || (n && !pixels))
        return NULL;
    owner = create_texture(host);
    if (!owner)
        return NULL;
    owner->texture.pixels = pixels;
    owner->texture.description = d;
    s->publish(s->context, owner, &owner->texture);
    return owner;
}

MMTextureOwner *mm_texture_wrap(void *imported, const MMTextureOwnerHost *host)
{
    const MMTextureServices *s = host->services;
    MMTextureOwner *owner;
    MMTextureResource *texture;
    if (!imported)
        return NULL;
    owner = create_texture(host);
    if (!owner)
        return NULL;
    texture = &owner->texture;
    texture->imported_owner = imported;
    s->describe_import(s->context, imported, &texture->description, &texture->pixels);
    s->publish(s->context, owner, texture);
    return owner;
}

static int load(MMTextureOwner *owner, const MMReadStream *stream)
{
    const MMTextureServices *s = owner->host->services;
    MMTextureResource *m = &owner->texture;
    unsigned char b[20];
    MMTextureDescription d;
    MMI32 n;
    int bad, valid;
    if (!mm_read_exact(stream, 0, b, sizeof(b)))
        return 0;
    valid = mm_texture_header_read(b, s->host_big_endian, &d, &bad);
    if (bad)
        s->diagnostic(s->context, MM_TEXTURE_ORDER);
    if (!valid || !mm_texture_payload(&d, &n))
        return 0;
    if (!s->allocate(s->context, n, 1, &m->pixels))
        return 0;
    m->description = d;
    s->publish(s->context, owner, m);
    return mm_read_exact(stream, sizeof(b), m->pixels, (size_t)n);
}
MMTextureOwner *mm_texture_read_stream(const MMReadStream *stream, const MMTextureOwnerHost *host)
{
    MMTextureOwner *owner = create_texture(host);
    if (owner && !load(owner, stream))
    {
        mm_texture_owner_release(owner);
        owner = NULL;
    }
    return owner;
}

static int load_block(MMTextureOwner *owner, MMOwnedBlock *block)
{
    const MMChunkPayloadServices *payloads = owner->host->services->payloads;
    MMBlockIO io;
    MMReadStream stream;
    return mm_owned_block_code(block, 0, payloads->decode_context, payloads->decode, NULL) == MM_DATA_OK &&
           mm_owned_block_io(block, &io) == MM_DATA_OK && mm_block_io_stream(&io, &stream) == MM_DATA_OK &&
           load(owner, &stream);
}

MMTextureOwner *mm_texture_read(void *file, MMU32 type, MMU32 number, const MMTextureOwnerHost *host)
{
    const MMChunkPayloadServices *payloads = host->services->payloads;
    MMOwnedBlock block;
    MMTextureOwner *owner;
    if (mm_owned_block_init(&block, payloads->handles, payloads->files) != MM_DATA_OK)
        return NULL;
    owner = create_texture(host);
    if (owner && (mm_chunk_payload_bind_block(payloads, file, type, number, &block) != MM_CHUNK_PAYLOAD_OK ||
                  !load_block(owner, &block)))
    {
        mm_texture_owner_release(owner);
        owner = NULL;
    }
    mm_owned_block_clear(&block);
    return owner;
}

int mm_texture_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMTextureOwnerHost *host)
{
    const MMTextureServices *s = host->services;
    MMTextureOwner *owner;
    *bytes = (MMI32)block->view.length;
    if (!out)
        return 1;
    owner = create_texture(host);
    if (!owner || !load_block(owner, block))
    {
        mm_texture_owner_release(owner);
        s->invalidate(s->context, out, sizeof(*out));
        s->invalidate(s->context, bytes, sizeof(*bytes));
        return 0;
    }
    *out = owner;
    return 1;
}

int mm_texture_write(const MMTextureResource *m, MMOwnedBlock *block, const MMTextureServices *s)
{
    MMBlockIO io;
    unsigned char b[20];
    MMI32 n;
    if (!mm_texture_payload(&m->description, &n))
        return 0;
    mm_texture_header_write(b, s->host_big_endian, s->system, &m->description);
    if (mm_owned_block_io(block, &io) != MM_DATA_OK || mm_block_write(&io, 0, b, sizeof(b), 0) != MM_DATA_OK)
        return 0;
    return mm_block_write(&io, sizeof(b), m->pixels, (MMU32)n, 0) == MM_DATA_OK;
}
int mm_texture_write_chunk(const MMTextureResource *m, const MMChunkEdit *edit, MMU32 type, MMU32 *number,
                           const MMTextureServices *s)
{
    MMOwnedBlock block;
    MMI32 bytes = mm_texture_size(m);
    int ok = 0;
    if (bytes < 0 || mm_owned_block_init(&block, s->payloads->handles, s->payloads->files) != MM_DATA_OK)
        return 0;
    if (mm_chunk_add_block(edit, type, (MMU32)bytes, number, &block) == MM_CHUNK_OK)
        ok = mm_texture_write(m, &block, s);
    else
        s->invalidate(s->context, number, sizeof(*number));
    mm_owned_block_clear(&block);
    return ok;
}

static int copy_to_file(MMOwnedBlock *source, void *file, const MMStorageAllocator *allocator)
{
    MMOwnedBlock destination;
    MMBlockFile extent;
    MMBlockIO from, to;
    int ok;
    if (mm_owned_block_init(&destination, source->handles, source->files) != MM_DATA_OK)
        return 0;
    extent.file = file;
    extent.offset = 4;
    extent.length = source->view.length;
    ok = mm_owned_block_set_file(&destination, &extent, 0) == MM_DATA_OK &&
         mm_owned_block_io(source, &from) == MM_DATA_OK && mm_owned_block_io(&destination, &to) == MM_DATA_OK &&
         mm_block_copy(&from, &to, 1, allocator) == MM_DATA_OK;
    mm_owned_block_clear(&destination);
    return ok;
}

int mm_texture_standalone(const MMTextureResource *m, void *filename, int compress, void *sink,
                                const MMTextureServices *s)
{
    const MMChunkPayloadServices *payloads = s->payloads;
    const MMBlockFileServices *files = payloads->files;
    MMOwnedBlock block;
    void *file = s->create_file(s->context, filename);
    MMI32 bytes = mm_texture_size(m);
    MMU32 signature = s->unpacked_signature;
    unsigned char b[20];
    int error = MM_TEXTURE_FILE_CREATE, ok = 0, shrink_failed, initialized = 0;
    if (!file)
        goto done;
    error = MM_TEXTURE_FILE_MEMORY;
    if (bytes < 0)
        goto done;
    if (compress)
    {
        initialized = mm_owned_block_init(&block, payloads->handles, files) == MM_DATA_OK;
        if (!initialized || mm_owned_block_set_temp(&block, (MMU32)bytes, 0) != MM_DATA_OK ||
            !mm_texture_write(m, &block, s))
            goto done;
        shrink_failed = 0;
        if (mm_owned_block_code(&block, 1, s->context, s->encode, &shrink_failed) == MM_DATA_OK)
            signature = s->packed_signature;
        if (shrink_failed)
            s->diagnostic(s->context, MM_TEXTURE_PACK_SHRINK);
        put(b, 4, signature, s->host_big_endian);
        error = MM_TEXTURE_FILE_WRITE;
        if (!files->write(files->context, file, 0, b, 4) || !copy_to_file(&block, file, s->copy_allocator))
            goto done;
    }
    else
    {
        put(b, 4, signature, s->host_big_endian);
        error = MM_TEXTURE_FILE_WRITE;
        if (!files->write(files->context, file, 0, b, 4))
            goto done;
        mm_texture_header_write(b, s->host_big_endian, s->system, &m->description);
        if (!files->write(files->context, file, 4, b, 20) ||
            !files->write(files->context, file, 24, m->pixels, (size_t)bytes - 20))
            goto done;
    }
    ok = 1;
done:
    if (initialized)
        mm_owned_block_clear(&block);
    if (!ok && sink)
        s->report(s->context, sink, error);
    if (file)
    {
        if (!ok)
            s->make_temporary(s->context, file);
        s->release_aux(s->context, file);
    }
    return ok;
}
