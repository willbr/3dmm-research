#include "mm/model_resource.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fixture
{
    MMModelResource owner;
    MMModelDescription description;
    unsigned char block[144], vertices[64], written[144];
    MMModelFace face;
    MMI32 points[2][3];
    int size, fail, step, objects, models, memory, adds, removes, invalid, warnings, bad;
    int components, published, bound, reads, prepared, flags, identified, geometry;
    int file_refs, decodes;
    MMChunkEntry chunk;
    MMU32 chunks, freed;
    int resets;
} Fixture;
typedef struct TestHandle
{
    MMU32 size;
    unsigned char data[144];
} TestHandle;
static int next(Fixture *f)
{
    return ++f->step != f->fail;
}
static MMModelResource *state(void *p, void *object)
{
    Fixture *f = (Fixture *)p;
    assert(object == f);
    return &f->owner;
}
static void *create(void *p)
{
    Fixture *f = (Fixture *)p;
    if (!next(f))
        return NULL;
    ++f->objects;
    mm_model_init(&f->owner);
    return f;
}
static void remove_model(void *p, void *h)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description);
    ++f->removes;
}
static void free_model(void *p, void *h)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description);
    --f->models;
}
static void object_release(void *p, void *object)
{
    Fixture *f = (Fixture *)p;
    assert(object == f);
    if (f->owner.handle)
    {
        remove_model(p, f->owner.handle);
        free_model(p, f->owner.handle);
        f->owner.handle = NULL;
    }
    --f->objects;
}
static int handle_allocate(void *p, MMU32 bytes, void **out)
{
    TestHandle *handle = (TestHandle *)malloc(sizeof(*handle));
    (void)p;
    assert(handle && bytes <= sizeof(handle->data));
    handle->size = bytes;
    *out = handle;
    return 1;
}
static void handle_release(void *p, void *handle)
{
    (void)p;
    free(handle);
}
static MMU32 handle_size(void *p, void *handle)
{
    (void)p;
    return ((TestHandle *)handle)->size;
}
static void *handle_lock(void *p, void *handle)
{
    (void)p;
    return ((TestHandle *)handle)->data;
}
static void handle_unlock(void *p, void *handle)
{
    (void)p;
    (void)handle;
}
static int handle_resize(void *p, void **handle, MMU32 bytes)
{
    TestHandle *h = (TestHandle *)*handle;
    (void)p;
    assert(bytes <= sizeof(h->data));
    h->size = bytes;
    return 1;
}
static void retain_file(void *p, void *file)
{
    (void)p;
    ++((Fixture *)file)->file_refs;
}
static void release_file(void *p, void *file)
{
    Fixture *f = (Fixture *)file;
    (void)p;
    assert(f->file_refs == 1);
    --f->file_refs;
}
static void *temp_file(void *p)
{
    (void)p;
    return NULL;
}
static int read_bytes(void *p, void *file, MMU32 at, void *out, size_t bytes)
{
    Fixture *f = (Fixture *)file;
    (void)p;
    assert(f->file_refs == 1 && at >= 3 && at - 3 + bytes <= (size_t)f->size);
    ++f->reads;
    if (!next(f))
        return 0;
    memcpy(out, f->block + (at - 3), bytes);
    return 1;
}
static int write_bytes(void *p, void *file, MMU32 at, const void *data, size_t bytes)
{
    (void)p;
    (void)file;
    (void)at;
    (void)data;
    (void)bytes;
    return 0;
}
static int decode(void *p, void **handle)
{
    Fixture *f = (Fixture *)p;
    assert(handle_size(NULL, *handle) == 12);
    ++f->decodes;
    if (!next(f))
        return 0;
    handle_resize(NULL, handle, (MMU32)f->size);
    memcpy(handle_lock(NULL, *handle), f->block, (size_t)f->size);
    return 1;
}
static void model_block(MMOwnedBlock *block, Fixture *f, int packed)
{
    static const MMHandleServices handles = {
        NULL, handle_allocate, handle_release, handle_size, handle_lock, handle_unlock, handle_resize
    };
    static const MMBlockFileServices files = {NULL, retain_file, release_file, temp_file, read_bytes, write_bytes};
    MMBlockFile file;
    file.file = f;
    file.offset = 3;
    file.length = packed ? 12 : (MMU32)f->size;
    assert(mm_owned_block_init(block, &handles, &files) == MM_DATA_OK);
    assert(mm_owned_block_set_file(block, &file, packed) == MM_DATA_OK);
}
static void *allocate(void *p, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Fixture *f = (Fixture *)p;
    void *out;
    assert(!old && !old_bytes && bytes && priority == MM_STORAGE_NORMAL);
    if (!next(f))
        return NULL;
    out = malloc(bytes);
    assert(out);
    memset(out, 0xcc, bytes);
    ++f->memory;
    return out;
}
static void free_memory(void *p, void *bytes, MMU32 size)
{
    Fixture *f = (Fixture *)p;
    assert(bytes && size && f->memory > 0);
    free(bytes);
    --f->memory;
}
static void invalidate(void *p, void *out, size_t bytes)
{
    Fixture *f = (Fixture *)p;
    assert(!f->objects && !f->models);
    ++f->invalid;
    memset(out, 0x55, bytes);
}
static void diagnostic(void *p, int code)
{
    Fixture *f = (Fixture *)p;
    assert(code == MM_MODEL_BAD_ORDER || code == MM_MODEL_BAD_COUNT);
    ++f->bad;
}
static void warning(void *p)
{
    ++((Fixture *)p)->warnings;
}
static void *allocate_model(void *p, MMI32 vertices, MMI32 faces)
{
    Fixture *f = (Fixture *)p;
    assert((vertices == 0 || vertices == 2) && (faces == 0 || faces == 1));
    if (!next(f))
        return NULL;
    ++f->models;
    return &f->description;
}
static void identify(void *p, void *h, void *object)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && p == object);
    ++f->identified;
}
static void copy_input(void *p, void *h, const void *v, const void *faces, MMI32 nv, MMI32 nf)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && nv == 2 && nf == 1 && v && faces);
}
static void add(void *p, void *h)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description);
    ++f->adds;
}
static int component(void *p, void *h, int kind, MMI32 bytes)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && kind >= 0 && kind <= 3 && bytes > 0);
    ++f->components;
    return next(f);
}
static void *vertex_data(void *p, void *h, int prepared)
{
    Fixture *f = (Fixture *)p;
    (void)prepared;
    assert(h == &f->description);
    return f->vertices;
}
static void set_face(void *p, void *h, int prepared, MMI32 index, const MMModelFace *face)
{
    Fixture *f = (Fixture *)p;
    (void)prepared;
    assert(h == &f->description && index == 0);
    f->face = *face;
}
static void get_face(void *p, void *h, MMI32 index, MMModelFace *face)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && index == 0);
    *face = f->face;
}
static void publish(void *p, void *h, const MMModelDescription *d, unsigned flag, MMI32 vg, MMI32 fg)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && flag == 64 && vg == 1 && fg == 1);
    f->description.vertices = d->vertices;
    f->description.faces = d->faces;
    ++f->published;
}
static void bind(void *p, void *h, int face)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && (face == 0 || face == 1));
    ++f->bound;
}
static void describe(void *p, void *h, MMModelDescription *d)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description);
    *d = f->description;
}
static void geometry(void *p, void *h, const MMModelDescription *d)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description);
    f->description = *d;
    ++f->geometry;
}
static MMU32 chunk_count(void *p) { return ((Fixture *)p)->chunks; }
static int chunk_entry(void *p, MMU32 i, MMChunkEntry *out)
{
    Fixture *f = (Fixture *)p;
    assert(i == 0 && f->chunks == 1);
    *out = f->chunk;
    return 1;
}
static int insert_chunk(void *p, MMU32 i)
{
    Fixture *f = (Fixture *)p;
    assert(i == 0 && !f->chunks);
    f->chunks = 1;
    return 1;
}
static void store_chunk(void *p, MMU32 i, const MMChunkEntry *entry, int initialize)
{
    Fixture *f = (Fixture *)p;
    assert(i == 0 && f->chunks == 1 && (!initialize || !f->chunk.bytes));
    f->chunk = *entry;
}
static void erase_chunk(void *p, MMU32 i)
{
    Fixture *f = (Fixture *)p;
    assert(i == 0 && f->chunks == 1);
    f->chunks = 0;
    memset(&f->chunk, 0, sizeof(f->chunk));
}
static int allocate_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    Fixture *f = (Fixture *)p;
    assert(f->chunks == 1 && bytes == sizeof(f->written));
    if (!next(f))
        return 0;
    extent->file = p;
    extent->offset = f->chunk.offset == 200 ? 400 : 200;
    extent->bytes = bytes;
    extent->flags = 0;
    return 1;
}
static void free_extent(void *p, int extra, MMU32 offset, MMU32 bytes)
{
    Fixture *f = (Fixture *)p;
    assert(!extra && (offset == 200 || offset == 400) && bytes == sizeof(f->written));
    f->freed = offset;
}
static void reset_chunk(void *p, MMU32 type, MMU32 number)
{
    Fixture *f = (Fixture *)p;
    assert(type == 3 && number == 7);
    ++f->resets;
}
static int write_model_bytes(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    Fixture *f = (Fixture *)p;
    assert(file == f && (offset == 200 || offset == 400) && bytes == sizeof(f->written));
    if (!next(f))
        return 0;
    memcpy(f->written, data, (size_t)bytes);
    return 1;
}
static void position(void *p, void *h, MMI32 i, MMI32 out[3])
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && i < 2);
    memcpy(out, f->points[i], sizeof(f->points[i]));
}
static void set_position(void *p, void *h, MMI32 i, const MMI32 in[3])
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && i < 2);
    memcpy(f->points[i], in, sizeof(f->points[i]));
}
static const void *path(void *p, void *filename)
{
    assert(p == filename);
    return p;
}
static void *load_dat(void *p, const void *path_value)
{
    assert(p == path_value);
    return allocate_model(p, 0, 0);
}
static void flags(void *p, void *h, unsigned value)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && value == 2);
    ++f->flags;
}
static void prepare(void *p, void *h, unsigned value)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && value == 31);
    ++f->prepared;
}
static void init(Fixture *f, int big, int prepared)
{
    MMModelFace face;
    int i;
    memset(f, 0, sizeof(*f));
    f->size = 144;
    /* Header 1/OS/2 vertices/1 face, native or reversed. */
    f->block[big ? 1 : 0] = 1;
    f->block[big ? 5 : 4] = 2;
    f->block[big ? 7 : 6] = 1;
    if (prepared)
        f->block[big ? 11 : 8] = 1;
    memset(&face, 0, sizeof(face));
    face.vertices[1] = 1;
    face.vertices[2] = 1;
    face.edges[0] = 65535;
    face.smoothing = 0x1234;
    face.flags = 7;
    face.pad = 0x38;
    face.normal[0] = -32768;
    face.normal[1] = -3;
    face.normal[2] = 32767;
    face.distance = -12345;
    mm_model_face_write(f->block + 112, big, &face);
    /* Discard input material and implicit padding, preserve explicit _pad0. */
    f->block[124] = 19;
    f->block[138] = 0x73;
    f->block[139] = 0x52;
    for (i = 0; i < 64; ++i)
        f->block[48 + i] = (unsigned char)(i * 3 + 1);
}
static void load_tests(MMModelServices *s)
{
    Fixture *f = (Fixture *)s->context;
    MMOwnedBlock block;
    int big, prepared, fail, ok, steps;
    MMI32 bytes;
    void *out;
    MMModelDescription d;
    int bad;
    for (big = 0; big < 2; ++big)
        for (prepared = 0; prepared < 2; ++prepared)
        {
            s->host_big_endian = big;
            init(f, big, prepared);
            model_block(&block, f, 0);
            assert(mm_model_fetch(&block, &out, &bytes, s));
            assert(block.view.kind == MM_BLOCK_FILE && f->file_refs == 1 && !f->decodes);
            assert(mm_owned_block_clear(&block) == MM_DATA_OK && !f->file_refs);
            steps = f->step;
            assert(out == f && bytes == 144 && f->adds == 1 && f->identified == 1 &&
                   !memcmp(f->vertices, f->block + 48, 64));
            assert(f->published == prepared && f->bound == prepared * 2 && f->components == prepared * 4);
            assert(f->face.normal[0] == -32768 && f->face.normal[1] == -3 && f->face.normal[2] == 32767 &&
                   f->face.pad == 0x38 && f->face.edges[0] == 65535);
            object_release(f, f);
            assert(!f->models && !f->objects && !f->memory);
            for (fail = 1; fail <= steps; ++fail)
            {
                init(f, big, prepared);
                f->fail = fail;
                bytes = 9;
                out = NULL;
                model_block(&block, f, 0);
                ok = mm_model_fetch(&block, &out, &bytes, s);
                assert(mm_owned_block_clear(&block) == MM_DATA_OK && !f->file_refs);
                assert(!ok && f->invalid == 2 && !f->objects && !f->models && !f->memory);
            }
            init(f, big, prepared);
            model_block(&block, f, 1);
            assert(mm_model_fetch(&block, NULL, &bytes, s) && bytes == 12 && !f->step && block.view.packed);
            assert(mm_model_fetch(&block, &out, &bytes, s) && bytes == 144 && out == f && f->decodes == 1);
            assert(block.view.kind == MM_BLOCK_MEMORY && !block.view.packed && !f->file_refs);
            assert(mm_owned_block_clear(&block) == MM_DATA_OK);
            object_release(f, f);
            assert(!f->models && !f->objects && !f->memory);
            /* Foreign header uses host-native payload unchanged, as legacy did. */
            init(f, big, prepared);
            {
                unsigned char header[48];
                int i;
                memcpy(header, f->block, 48);
                for (i = 0; i < 8; i += 2)
                {
                    f->block[i] = header[i + 1];
                    f->block[i + 1] = header[i];
                }
                for (i = 8; i < 48; i += 4)
                {
                    f->block[i] = header[i + 3];
                    f->block[i + 1] = header[i + 2];
                    f->block[i + 2] = header[i + 1];
                    f->block[i + 3] = header[i];
                }
            }
            model_block(&block, f, 0);
            assert(mm_model_fetch(&block, &out, &bytes, s) && f->face.normal[0] == -32768 &&
                   !memcmp(f->vertices, f->block + 48, 64));
            assert(mm_owned_block_clear(&block) == MM_DATA_OK && !f->file_refs);
            object_release(f, f);
        }
    s->host_big_endian = 0;
    init(f, 0, 1);
    model_block(&block, f, 1);
    f->fail = 2;
    assert(!mm_model_fetch(&block, &out, &bytes, s) && f->decodes == 1 && f->invalid == 2);
    assert(block.view.kind == MM_BLOCK_FILE && block.view.packed && f->file_refs == 1);
    assert(mm_owned_block_clear(&block) == MM_DATA_OK && !f->file_refs);
    init(f, 0, 1);
    for (fail = 0; fail < 144; ++fail)
    {
        f->size = fail;
        mm_model_init(&f->owner);
        f->objects = 1;
        model_block(&block, f, 0);
        assert(!mm_model_read(f, &block, s));
        assert(mm_owned_block_clear(&block) == MM_DATA_OK && !f->file_refs);
        object_release(f, f);
        assert(!f->models && !f->memory);
    }
    init(f, 0, 1);
    f->block[4] = 0xff;
    f->block[5] = 0xff;
    assert(!mm_model_header(f->block, 0, &d, &bad) && !bad && d.vertices == -1);
    f->objects = 1;
    model_block(&block, f, 0);
    assert(!mm_model_read(f, &block, s) && !f->owner.handle && f->bad == 1);
    assert(mm_owned_block_clear(&block) == MM_DATA_OK && !f->file_refs);
    object_release(f, f);
    init(f, 0, 1);
    f->block[0] = 2;
    f->objects = 1;
    model_block(&block, f, 1);
    assert(mm_model_read(f, &block, s) && f->bad == 1 && f->decodes == 1);
    assert(mm_owned_block_clear(&block) == MM_DATA_OK && !f->file_refs);
    object_release(f, f);
}
static void mesh_tests(void)
{
    const unsigned short one = 1;
    const int big = *(const unsigned char *)&one == 0;
    Fixture f;
    MMMemoryStream input;
    MMModelMesh mesh;
    MMModelVertex *previous;
    int header_order;
    for (header_order = 0; header_order < 2; ++header_order)
    {
        init(&f, big, 1);
        if (header_order != big)
        {
            f.block[big ? 1 : 0] = f.block[big ? 5 : 4] = f.block[big ? 7 : 6] = f.block[big ? 11 : 8] = 0;
            f.block[big ? 0 : 1] = f.block[big ? 6 : 7] = f.block[big ? 8 : 11] = 1;
            f.block[big ? 4 : 5] = 2;
        }
        assert(mm_memory_stream_init(&input, f.block, (MMU32)f.size));
        assert(mm_model_mesh_read(&input.stream, &mesh) == MM_MODEL_MESH_OK);
        assert(mesh.description.radius == 1 && mesh.description.vertices == 2 && mesh.description.faces == 1);
        assert(mesh.vertices[0].position[0] == (big ? 0x0104070aL : 0x0a070401L));
        assert(mesh.vertices[0].red == 64 && mesh.faces[0].vertices[2] == 1 && mesh.faces[0].normal[1] == -3);
        previous = mesh.vertices;
        --input.stream.length;
        assert(mm_model_mesh_read(&input.stream, &mesh) == MM_MODEL_MESH_BAD_FILE && mesh.vertices == previous);
        ++input.stream.length;
        f.block[112 + (big ? 1 : 0)] = 2;
        assert(mm_model_mesh_read(&input.stream, &mesh) == MM_MODEL_MESH_BAD_FILE && mesh.vertices == previous);
        mm_model_mesh_dispose(&mesh);
    }
}

static void author_tests(MMModelServices *s)
{
    Fixture *f = (Fixture *)s->context;
    MMChunkEdit edit = {0};
    MMBlockFileServices files = {0};
    MMModelFace face;
    MMModelDescription d;
    int big, bad, fail, existing;
    void *object;
    edit.context = f;
    edit.count = chunk_count;
    edit.entry = chunk_entry;
    edit.insert = insert_chunk;
    edit.store = store_chunk;
    edit.erase = erase_chunk;
    edit.allocate = allocate_extent;
    edit.free_space = free_extent;
    edit.reset_runtime = reset_chunk;
    files.context = f;
    files.write = write_model_bytes;
    for (big = 0; big < 2; ++big)
    {
        s->host_big_endian = big;
        init(f, big, 1);
        f->owner.handle = &f->description;
        f->description.vertices = 2;
        f->description.faces = 1;
        f->description.radius = 7;
        f->description.bounds.min[0] = -3;
        f->description.bounds.max[0] = 11;
        mm_model_face_read(f->block + 112, big, &f->face);
        memcpy(f->vertices, f->block + 48, 64);
        assert(mm_model_write(&f->owner, &edit, &files, 3, 7, s) && !f->memory);
        assert(f->chunks == 1 && f->chunk.offset == 200 && f->chunk.bytes == sizeof(f->written));
        assert(mm_model_header(f->written, big, &d, &bad) && !bad && d.vertices == 2 && d.faces == 1 &&
               d.radius == 7 && d.bounds.min[0] == -3);
        assert(!memcmp(f->written + 48, f->vertices, 64));
        mm_model_face_read(f->written + 112, big, &face);
        assert(!memcmp(&face, &f->face, sizeof(face)));
        assert(!f->written[124] && !f->written[125] && !f->written[126] && !f->written[127] && !f->written[138] &&
               !f->written[139]);
        f->chunk.flags |= MM_CHUNK_PACKED;
        f->chunk.parent_count = 2;
        f->description.radius = 9;
        assert(mm_model_write(&f->owner, &edit, &files, 3, 7, s) && f->freed == 200 && f->resets == 1);
        assert(f->chunk.offset == 400 && f->chunk.flags == (MM_CHUNK_LONER | MM_CHUNK_PACKED) && f->chunk.parent_count == 2);
        assert(mm_model_header(f->written, big, &d, &bad) && d.radius == 9);
        for (existing = 1; existing >= 0; --existing)
        {
            for (fail = 1; fail <= 3; ++fail)
            {
                f->step = f->warnings = f->resets = 0;
                f->freed = 0;
                f->fail = fail;
                assert(!mm_model_write(&f->owner, &edit, &files, 3, 7, s) && f->warnings == 1 && !f->memory);
                assert(f->chunks == (MMU32)existing && f->freed == (fail == 3 ? 200U : 0U));
                assert(f->resets == (!existing && fail == 3));
                if (existing)
                    assert(f->chunk.offset == 400 && f->chunk.parent_count == 2);
            }
            if (existing)
                erase_chunk(f, 0);
        }
    }
    s->host_big_endian = 0;
    for (fail = 0; fail < 4; ++fail)
    {
        init(f, 0, 0);
        f->fail = fail;
        object = mm_model_new(2, f->vertices, 1, &f->face, s);
        if (object)
        {
            assert(f->adds == 1 && f->identified == 1);
            object_release(f, f);
        }
        assert(!f->objects && !f->models);
        init(f, 0, 0);
        f->fail = fail;
        object = mm_model_dat(f, s);
        if (object)
        {
            assert(f->prepared == 1 && f->flags == 1 && f->identified == 1 && !f->adds);
            object_release(f, f);
        }
        assert(!f->objects && !f->models);
    }
    init(f, 0, 0);
    f->owner.handle = &f->description;
    f->models = 1;
    mm_model_dispose(&f->owner, s);
    mm_model_dispose(&f->owner, s);
    assert(!f->models && f->removes == 1);
    /* Odd negative widths use arithmetic-half floor, not truncation to zero. */
    init(f, 0, 0);
    f->owner.handle = &f->description;
    f->description.vertices = 2;
    f->description.bounds.min[0] = 1;
    f->description.bounds.max[0] = -2;
    f->description.bounds.min[2] = 0;
    f->description.bounds.max[2] = -3;
    f->points[0][0] = 10;
    f->points[0][1] = 11;
    f->points[0][2] = 12;
    mm_model_adjust_glyph(&f->owner, s);
    assert(f->points[0][0] == 11 && f->points[0][1] == 11 && f->points[0][2] == 14);
    assert(f->description.bounds.min[0] == 1 && f->description.bounds.max[0] == 0 && f->description.pivot[2] == 2);
    assert(mm_model_extent(&f->owner, 0, s) == -1);
    init(f, 0, 0);
    assert(!mm_model_new(-1, f->vertices, 1, &f->face, s) && !f->step);
    assert(!mm_model_new(65535L, f->vertices, 1, &f->face, s) && !f->step);
    f->owner.handle = &f->description;
    f->description.bounds.min[0] = -655;
    f->description.bounds.max[0] = 655;
    mm_model_adjust_glyph(&f->owner, s);
    assert(f->description.bounds.max[0] == 0);
    f->description.bounds.min[0] = -656;
    f->description.bounds.max[0] = 656;
    mm_model_adjust_glyph(&f->owner, s);
    assert(f->description.bounds.max[0] == 33424);
}
typedef struct Lighting
{
    int fail, step, live, enabled, added, rendering, lit, diagnostics;
    int nodes[16], next;
} Lighting;
static void *light_create(void *p, int kind, const MMModelLightingSpec *spec)
{
    Lighting *l = (Lighting *)p;
    assert(spec->fov == 10920 && spec->width == 544 && spec->height == 306 && spec->aspect == 116508 &&
           spec->camera_width == 544);
    assert(spec->ambient == 6553 && spec->diffuse == 39321 && !spec->specular && spec->colour == 0xffffff &&
           spec->material_lit && spec->material_gouraud);
    if (++l->step == l->fail)
        return NULL;
    l->nodes[l->next] = kind;
    ++l->live;
    return l->nodes + l->next++;
}
static void attach(void *p, void *world, void *actor)
{
    Lighting *l = (Lighting *)p;
    assert(world == l->nodes && actor);
}
static void enable(void *p, void *actor)
{
    assert(actor);
    ++((Lighting *)p)->enabled;
}
static void disable(void *p, void *actor)
{
    assert(actor);
    --((Lighting *)p)->enabled;
}
static void add_material(void *p, void *material)
{
    assert(material);
    ++((Lighting *)p)->added;
}
static void remove_material(void *p, void *material)
{
    assert(material);
    --((Lighting *)p)->added;
}
static void begin(void *p, void *w, void *c, void *colour, void *depth)
{
    Lighting *l = (Lighting *)p;
    assert(w && c && colour && depth && l->added && !l->rendering);
    l->rendering = 1;
}
static void light(void *p, void *model, void *material, void *world)
{
    Lighting *l = (Lighting *)p;
    assert(model && material && world && l->rendering);
    ++l->lit;
}
static void end(void *p)
{
    Lighting *l = (Lighting *)p;
    assert(l->rendering);
    l->rendering = 0;
}
static void light_free(void *p, int kind, void *object)
{
    Lighting *l = (Lighting *)p;
    int i;
    assert(object && !l->rendering && !l->enabled && !l->added);
    if (kind == MM_MODEL_LIGHT_WORLD)
    {
        for (i = 0; i < l->next; ++i)
            if (l->nodes[i] <= MM_MODEL_LIGHT_ACTOR)
                --l->live;
    }
    else
        --l->live;
}
static void multiple(void *p)
{
    ++((Lighting *)p)->diagnostics;
}
static void lighting(void)
{
    MMModelLightingServices s;
    MMModelResource m;
    Lighting l;
    int count, fail, total;
    memset(&s, 0, sizeof(s));
    s.context = &l;
    s.create = light_create;
    s.attach = attach;
    s.enable = enable;
    s.disable = disable;
    s.add_material = add_material;
    s.remove_material = remove_material;
    s.begin = begin;
    s.light = light;
    s.end = end;
    s.free = light_free;
    s.multiple_lights = multiple;
    m.handle = &m;
    for (count = 1; count < 10; ++count)
    {
        memset(&l, 0, sizeof(l));
        assert(mm_model_prelight(&m, count, &s));
        total = l.step;
        assert(!l.live && !l.enabled && !l.added && l.lit == 1 && l.diagnostics == (count != 1));
        for (fail = 1; fail <= total; ++fail)
        {
            memset(&l, 0, sizeof(l));
            l.fail = fail;
            assert(!mm_model_prelight(&m, count, &s) && !l.live && !l.enabled && !l.added && !l.lit);
        }
    }
    memset(&l, 0, sizeof(l));
    assert(!mm_model_prelight(&m, 0, &s) && !mm_model_prelight(&m, 10, &s) && !l.step);
}
int main(void)
{
    Fixture f;
    MMModelServices s;
    MMStorageAllocator allocator = {NULL, allocate, free_memory};
    allocator.context = &f;
    memset(&s, 0, sizeof(s));
    s.context = &f;
    s.face_bytes = 36;
    s.vertex_group_bytes = 24;
    s.face_group_bytes = 24;
    s.prepared_flag = 64;
    s.keep_original_flag = 2;
    s.prepare_all = 31;
    s.state = state;
    s.create = create;
    s.release = object_release;
    s.decode = decode;
    s.allocator = &allocator;
    s.invalidate = invalidate;
    s.diagnostic = diagnostic;
    s.save_failed = warning;
    s.allocate_model = allocate_model;
    s.identify = identify;
    s.copy_input = copy_input;
    s.add = add;
    s.remove = remove_model;
    s.free_model = free_model;
    s.component = component;
    s.vertex_data = vertex_data;
    s.set_face = set_face;
    s.get_face = get_face;
    s.publish_prepared = publish;
    s.bind_group = bind;
    s.describe = describe;
    s.geometry = geometry;
    s.position = position;
    s.set_position = set_position;
    s.path = path;
    s.load_dat = load_dat;
    s.or_flags = flags;
    s.prepare = prepare;
    load_tests(&s);
    mesh_tests();
    author_tests(&s);
    lighting();
    puts("C89 model resources: wire codecs, factories, partial ownership, glyphs, saves and prelight cleanup passed.");
    return 0;
}
