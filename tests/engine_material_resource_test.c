#include "mm/material_resource.h"
#include "mm/template_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Ref
{
    int kind;
    MMReference refs;
    MMResourceObject binding;
} Ref;
typedef struct Handle
{
    MMMaterialDescription d;
    Ref *texture;
    void *raw;
    MMI32 matrix[6];
} Handle;
typedef struct Object
{
    Ref ref;
    MMMaterialResource material;
    MMCustomMaterial custom;
} Object;
typedef struct Test
{
    MMMaterialServices s;
    MMMaterialShade shade;
    MMResourceCache cache;
    MMReference cache_refs;
    MMHandleServices storage;
    MMOwnedBlock input;
    int step, fail, objects, handles, arrays, raw, file_refs, payload_handles, diagnostic, order[128], ordered;
    int texture_child, transform_child, custom, bad_model_target, adds, deletes[4], deleted;
    unsigned char header[20], transform_header[28], custom_header[9], written[20], written_transform[28];
    MMU32 header_size, transform_size, custom_size;
    Ref texture, material, model, shading;
    int texture_view, shade_view;
} Test;
static const char pix_path[] = "material-fixture.pix";
static Test *test(void *p)
{
    return (Test *)p;
}
static int fail(Test *t)
{
    return ++t->step == t->fail;
}
static void event(Test *t, int e)
{
    if (t->ordered < 128)
        t->order[t->ordered++] = e;
}
static void *create(void *p, int kind)
{
    Test *t = test(p);
    Object *o;
    if (fail(t))
        return NULL;
    o = (Object *)calloc(1, sizeof(*o));
    assert(o);
    o->ref.kind = kind;
    mm_reference_init(&o->ref.refs);
    ++t->objects;
    return o;
}
static void retain(void *p, void *o)
{
    (void)p;
    mm_reference_add(&((Ref *)o)->refs);
}
static void release(void *p, void *v)
{
    Test *t = test(p);
    Ref *r = (Ref *)v;
    assert(r && r->refs.count > 0);
    if (r->binding.cache)
    {
        mm_resource_object_release(r, &t->cache.services.objects);
        return;
    }
    if (mm_reference_drop(&r->refs))
        return;
    if (r->kind >= 100)
        return;
    event(t, 20 + r->kind);
    if (r->kind == MM_MATERIAL)
        mm_material_dispose(&((Object *)v)->material, &t->s);
    else
        mm_custom_material_dispose(&((Object *)v)->custom, &t->s);
    --t->objects;
    free(v);
}
static MMMaterialResource *material_state(void *p, void *o)
{
    (void)p;
    return &((Object *)o)->material;
}
static MMCustomMaterial *custom_state(void *p, void *o)
{
    (void)p;
    return &((Object *)o)->custom;
}
static void invalidate(void *p, void *out, size_t n)
{
    event(test(p), 10);
    memset(out, 0xa3, n);
}
static void diagnostic(void *p, int code)
{
    test(p)->diagnostic = code + 1;
}
static int resize_payload(void *p, void **handle, MMU32 bytes)
{
    MMU32 *v = (MMU32 *)realloc(*handle, sizeof(*v) + bytes);
    (void)p;
    if (!v)
        return 0;
    *v = bytes;
    *handle = v;
    return 1;
}
static int alloc_payload(void *p, MMU32 bytes, void **handle)
{
    *handle = NULL;
    if (!resize_payload(p, handle, bytes))
        return 0;
    ++test(p)->payload_handles;
    return 1;
}
static void free_payload(void *p, void *handle)
{
    --test(p)->payload_handles;
    free(handle);
}
static MMU32 payload_size(void *p, void *handle) { (void)p; return *(MMU32 *)handle; }
static void *lock_payload(void *p, void *handle) { (void)p; return (MMU32 *)handle + 1; }
static void unlock_payload(void *p, void *handle) { (void)p; (void)handle; }
static int decode(void *p, void **handle)
{
    assert(*handle);
    return !fail(test(p));
}

static void retain_file(void *file) { ++test(file)->file_refs; }
static void release_file(void *file) { --test(file)->file_refs; }
static void retain_block(void *p, void *file) { (void)p; retain_file(file); }
static void release_block(void *p, void *file) { (void)p; release_file(file); }
static void *no_temp(void *p) { (void)p; return NULL; }
static int no_write(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{
    (void)p; (void)file; (void)offset; (void)bytes; (void)count;
    return 0;
}
static int read_block(void *p, void *file, MMU32 offset, void *out, size_t count)
{
    Test *t = test(file);
    (void)p;
    if (fail(t))
        return 0;
    assert(count <= (offset == 64 ? t->custom_size : offset == 32 ? t->transform_size : t->header_size));
    memcpy(out, offset == 64 ? t->custom_header : offset == 32 ? t->transform_header : t->header, count);
    return 1;
}
static const MMBlockFileServices files = {NULL, retain_block, release_block, no_temp, read_block, no_write};

static int locate(void *file, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    Test *t = test(file);
    if (fail(t))
        return 0;
    out->file = file;
    out->offset = type == MM_CMTL ? 64 : type == 2 ? 32 : 0;
    out->length = type == MM_CMTL ? t->custom_size : type == 2 ? t->transform_size : type == 3 ? t->header_size : 0;
    *packed = out->length != 0;
    return number == 1 || number == 7;
}
static int contains(void *file, MMU32 type, MMU32 number)
{
    (void)file; (void)type;
    return number == 1 || number == 7;
}

static int material_entry(void *p, MMU32 index, MMChunkEntry *out)
{
    Test *t = test(p);
    if (index >= 3)
        return 0;
    memset(out, 0, sizeof(*out));
    out->type = index + 1;
    out->number = index == 2 ? 1 : 7;
    out->child_count = index == 2 ? t->texture_child + t->transform_child : 0;
    return 1;
}
static int material_child(void *p, MMU32 parent, MMU32 index, MMChunkChild *out)
{
    Test *t = test(p);
    if (parent != 2 || index >= (MMU32)(t->texture_child + t->transform_child))
        return 0;
    out->type = index == 0 && t->texture_child ? 1 : 2;
    out->number = 7;
    out->slot = 0;
    return 1;
}
static int costume_entry(void *p, MMU32 position, MMChunkEntry *out)
{
    static const MMU32 types[] = {MM_TEMPLATE_BMDL, MM_CMTL, MM_MATERIAL_CHUNK};
    (void)p;
    if (position >= sizeof(types) / sizeof(types[0]))
        return 0;
    memset(out, 0, sizeof(*out));
    out->type = types[position];
    out->number = position == 1 ? 1 : 7;
    out->child_count = position == 1 ? 3 : 0;
    return 1;
}
static int costume_child(void *p, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    static const MMU32 types[] = {MM_MATERIAL_CHUNK, MM_TEMPLATE_BMDL, 0x58585858};
    if (parent != 1 || position >= sizeof(types) / sizeof(types[0]))
        return 0;
    out->type = types[position];
    out->number = position == 1 && test(p)->bad_model_target ? 8 : 7;
    out->slot = position * 2;
    return 1;
}
static MMChunkAccess chunks(void *p)
{
    MMChunkAccess out;
    out.context = p;
    out.count = 3;
    out.entry = test(p)->custom ? costume_entry : material_entry;
    out.child = test(p)->custom ? costume_child : material_child;
    return out;
}
static void texture_reader(void) {}
static void material_reader(void) {}
static void model_reader(void) {}
static int compare_reader(MMResourceReader a, MMResourceReader b) { assert(a == b); return 0; }
static MMResourceObject *resource_state(void *o) { return &((Ref *)o)->binding; }
static MMReference *resource_refs(void *o) { return &((Ref *)o)->refs; }
static void destroy_resource(void *o) { assert(!((Ref *)o)->binding.cache); }
static void destroy_cache(void *p) { assert(mm_resource_cache_dispose(&test(p)->cache) == MM_STORAGE_OK); }
static int read_resource(void *p, MMResourceReader reader, MMU32 type, MMU32 number, MMOwnedBlock *block,
                         void **object, MMI32 *bytes)
{
    Test *t = test(p);
    Ref *r;
    assert(number == 7 && block->file == t);
    if (reader == texture_reader)
    {
        assert(!t->custom && type == 1);
        r = &t->texture;
    }
    else if (reader == material_reader)
    {
        assert(t->custom && type == MM_MATERIAL_CHUNK);
        r = &t->material;
    }
    else
    {
        assert(reader == model_reader && t->custom && type == MM_TEMPLATE_BMDL);
        r = &t->model;
    }
    if (fail(t))
        return 0;
    mm_reference_init(&r->refs);
    *object = r;
    *bytes = 1;
    return 1;
}
static int allocate(void *p, MMI32 n, void **out)
{
    Test *t = test(p);
    if (fail(t))
        return 0;
    *out = calloc(1, n ? (size_t)n : 1);
    assert(*out);
    ++t->arrays;
    return 1;
}
static void free_memory(void *p, void **v)
{
    if (*v)
    {
        --test(p)->arrays;
        free(*v);
        *v = NULL;
    }
}
static void *allocate_material(void *p)
{
    Test *t = test(p);
    Handle *h;
    if (fail(t))
        return NULL;
    h = (Handle *)calloc(1, sizeof(*h));
    assert(h);
    h->d.colour = 0xffffff;
    ++t->handles;
    return h;
}
static void identify(void *p, void *h, void *o)
{
    (void)p;
    (void)h;
    (void)o;
}
static void describe(void *p, void *h, MMMaterialDescription *d)
{
    (void)p;
    *d = ((Handle *)h)->d;
}
static void configure(void *p, void *h, const MMMaterialDescription *d)
{
    (void)p;
    ((Handle *)h)->d = *d;
}
static void add_material(void *p, void *h)
{
    (void)h;
    ++test(p)->adds;
}
static void remove_material(void *p, void *h)
{
    (void)p;
    (void)h;
}
static void free_material(void *p, void *h)
{
    --test(p)->handles;
    free(h);
}
static void *texture(void *p, void *h)
{
    (void)p;
    return ((Handle *)h)->texture;
}
static void *texture_pixelmap(void *p, void *t)
{
    Test *fixture = test(p);
    if (t == &fixture->texture)
        return &fixture->texture_view;
    assert(t == &fixture->shading);
    return &fixture->shade_view;
}
static void map(void *p, void *h, void *colour, void *shade)
{
    Test *t = test(p);
    Handle *handle = (Handle *)h;
    assert(!shade || shade == &t->shade_view);
    handle->texture = colour ? &t->texture : NULL;
    handle->raw = colour != &t->texture_view ? colour : NULL;
}
static MMI32 height(void *p, void *t)
{
    assert(t == &test(p)->shade_view);
    return 8;
}
static void transform(void *p, void *h, const MMI32 m[6])
{
    (void)p;
    memcpy(((Handle *)h)->matrix, m, 6 * sizeof(*m));
}
static int add_chunk(void *p, void *f, MMU32 type, const void *data, MMI32 bytes, MMU32 *number)
{
    Test *t = test(p);
    (void)f;
    assert(bytes == (type == t->s.transform_type ? 28 : 20));
    if (fail(t))
        return 0;
    if (type == t->s.transform_type)
    {
        memcpy(t->written_transform, data, 28);
        *number = 12;
    }
    else
    {
        memcpy(t->written, data, 20);
        *number = 10;
    }
    return 1;
}
static int write_texture(void *p, void *o, void *f, MMU32 type, MMU32 *number)
{
    (void)o;
    (void)f;
    (void)type;
    *number = 11;
    return !fail(test(p));
}
static int adopt(void *p, void *f, MMU32 type, MMU32 n, const MMMaterialChild *c)
{
    (void)f;
    (void)type;
    assert(n == 10 && c->id == 0 && c->number == (c->type == test(p)->s.transform_type ? 12U : 11U));
    return !fail(test(p));
}
static void delete_chunk(void *p, void *f, MMU32 type, MMU32 n)
{
    Test *t = test(p);
    (void)f;
    (void)type;
    assert(t->deleted < 4);
    t->deletes[t->deleted++] = (int)n;
}
static void *read_bitmap(void *p, void *filename, void *palette)
{
    Test *t = test(p);
    (void)filename;
    (void)palette;
    if (fail(t))
        return NULL;
    mm_reference_add(&t->texture.refs);
    return &t->texture;
}
static const void *pixelmap_path(void *p, void *filename)
{
    assert(p == filename);
    return pix_path;
}
static void *read_pixelmap(void *p, const void *path)
{
    Test *t = test(p);
    void *raw;
    assert(path == pix_path);
    if (fail(t))
        return NULL;
    raw = malloc(1);
    assert(raw);
    ++t->raw;
    return raw;
}
static void *wrap_pixelmap(void *p, void *raw)
{
    (void)raw;
    return read_bitmap(p, NULL, NULL);
}
static void free_pixelmap(void *p, void *raw)
{
    --test(p)->raw;
    free(raw);
}
static void *input_material(void *p, const void *input, MMI32 i)
{
    (void)p;
    return ((void *const *)input)[i];
}
static void *material_handle(void *p, void *o)
{
    assert(o == &test(p)->material);
    return o;
}
static void setup(Test *t)
{
    MMMaterialDescription d;
    MMResourceServices resources = {0};
    MMResourceSource source = {0};
    memset(t, 0, sizeof(*t));
    memset(&d, 0, sizeof(d));
    t->texture.kind = t->material.kind = t->model.kind = t->shading.kind = 100;
    t->s.context = t;
    t->s.system = t->s.windows_system = 0x0303;
    t->s.texture_type = 1;
    t->s.transform_type = 2;
    t->s.material_bytes = 32;
    t->s.custom_bytes = 56;
    t->s.light_flag = 1;
    t->s.gouraud_flag = 2;
    t->s.smooth_flag = 4;
    t->s.map_flag = 8;
    t->s.create = create;
    t->s.retain = retain;
    t->s.release = release;
    t->s.material_state = material_state;
    t->s.custom_state = custom_state;
    t->s.invalidate = invalidate;
    t->s.diagnostic = diagnostic;
    t->s.decode = decode;
    t->s.texture_reader = texture_reader;
    t->s.material_reader = material_reader;
    t->s.model_reader = model_reader;
    t->s.allocate = allocate;
    t->s.free_memory = free_memory;
    t->s.allocate_material = allocate_material;
    t->s.identify = identify;
    t->s.describe = describe;
    t->s.configure = configure;
    t->s.add_material = add_material;
    t->s.remove_material = remove_material;
    t->s.free_material = free_material;
    t->s.texture = texture;
    t->s.texture_pixelmap = texture_pixelmap;
    t->s.map = map;
    t->s.height = height;
    t->s.transform = transform;
    t->s.add_chunk = add_chunk;
    t->s.write_texture = write_texture;
    t->s.adopt = adopt;
    t->s.delete_chunk = delete_chunk;
    t->s.read_bitmap = read_bitmap;
    t->s.pixelmap_path = pixelmap_path;
    t->s.read_pixelmap = read_pixelmap;
    t->s.wrap_pixelmap = wrap_pixelmap;
    t->s.free_pixelmap = free_pixelmap;
    t->s.input_material = input_material;
    t->s.material_handle = material_handle;
    d.colour = 0x123456;
    d.ambient = 100;
    d.diffuse = 200;
    d.specular = 300;
    d.power = -1234567;
    d.index_base = 13;
    d.index_range = 19;
    mm_material_header_write(t->header, 0, 0x0303, &d);
    t->header_size = 20;
    t->transform_size = 28;
    t->custom_size = 8;
    t->transform_header[0] = 1;
    t->transform_header[4] = 7;
    t->custom_header[0] = 1;
    t->custom_header[4] = 3;
    mm_reference_init(&t->shading.refs);
    t->shade.texture = &t->shading;
    t->storage.context = t;
    t->storage.allocate = alloc_payload;
    t->storage.release = free_payload;
    t->storage.size = payload_size;
    t->storage.lock = lock_payload;
    t->storage.unlock = unlock_payload;
    t->storage.resize = resize_payload;
    resources.context = t;
    resources.objects.state = resource_state;
    resources.objects.references = resource_refs;
    resources.objects.destroy = destroy_resource;
    resources.compare_reader = compare_reader;
    resources.contains = contains;
    resources.live_chunks = chunks;
    resources.handles = &t->storage;
    resources.block_files = &files;
    resources.locate = locate;
    resources.read = read_resource;
    resources.retain_file = retain_file;
    resources.release_file = release_file;
    resources.destroy_owner = destroy_cache;
    source.live = t;
    mm_reference_init(&t->cache_refs);
    assert(mm_resource_cache_init(&t->cache, t, &t->cache_refs, &source, 64, &resources, NULL, 1) == MM_STORAGE_OK);
    assert(mm_owned_block_init(&t->input, &t->storage, &files) == MM_DATA_OK);
}
static void balanced(Test *t)
{
    assert(!t->objects && !t->handles && !t->arrays && !t->raw && !t->texture.refs.count &&
           !t->material.refs.count && !t->model.refs.count);
    assert(mm_owned_block_clear(&t->input) == MM_DATA_OK);
    mm_resource_cache_release(&t->cache);
    assert(!t->file_refs && !t->payload_handles);
}

static void primary_block(Test *t, MMU32 type)
{
    assert(mm_resource_cache_read_block(&t->cache, type, 1, &t->input) == MM_CHUNK_OK);
    t->step = 0;
}
static void costume_slots(void)
{
    Test t;
    MMChunkAccess graph;
    MMCustomMaterialPart part;
    Object *o = NULL;
    MMI32 bytes;
    setup(&t);
    t.custom = 1;
    graph = mm_resource_cache_chunks(&t.cache);
    assert(mm_custom_material_part(&graph, 1, 0, &part) == MM_CHUNK_OK);
    assert(part.material.type == MM_MATERIAL_CHUNK && part.material.number == 7 && !part.model.type);
    assert(mm_custom_material_part(&graph, 1, 4, &part) == MM_CHUNK_OK);
    assert(!part.material.type && !part.model.type);
    assert(mm_custom_material_part(&graph, 1, 2, &part) == MM_CHUNK_OK);
    assert(!part.material.type && part.model.type == MM_TEMPLATE_BMDL && part.model.number == 7);
    t.bad_model_target = 1;
    assert(mm_custom_material_part(&graph, 1, 2, &part) == MM_CHUNK_BAD_FILE);
    assert(!part.material.type && part.model.number == 7);
    primary_block(&t, MM_CMTL);
    assert(!mm_custom_material_fetch(&t.cache, MM_CMTL, 1, &t.input, &o, &bytes, &t.s));
    balanced(&t);
}
static void codecs(void)
{
    Test t;
    MMMaterialDescription d, e;
    unsigned char b[20];
    int big;
    setup(&t);
    for (big = 0; big < 2; ++big)
    {
        assert(mm_material_header_read(t.header, 0, 0x0303, &d));
        mm_material_header_write(b, big, 0x0303, &d);
        assert(mm_material_header_read(b, 0, 0x0303, &e));
        assert(e.power == -1234567 && e.diffuse == 200 && e.colour == 0x123456);
        b[0] = b[1] = 0;
        assert(mm_material_header_read(b, big, 0x0303, &e));
        b[2] = b[3] = 0;
        assert(!mm_material_header_read(b, big, 0x0303, &e));
    }
    balanced(&t);
}
static void loads(void)
{
    Test t;
    Object *o;
    MMI32 bytes;
    int n, ok;
    for (n = 0; n < 14; ++n)
    {
        setup(&t);
        t.texture_child = t.transform_child = 1;
        primary_block(&t, 3);
        t.fail = n;
        o = NULL;
        ok = mm_material_fetch(&t.cache, 3, 1, &t.input, &o, &bytes, &t.shade, &t.s);
        if (ok)
        {
            Handle *h = (Handle *)o->material.handle;
            assert(bytes == 32 && h->texture == &t.texture && h->d.index_range == 7 && h->matrix[0] == 7);
            assert(!t.input.view.packed && t.input.view.length == t.header_size && t.payload_handles == 1);
            release(&t, o);
        }
        else if (t.ordered >= 3 && t.order[2] == 20)
            assert(t.order[0] == 10 && t.order[1] == 10);
        balanced(&t);
    }
    for (n = 0; n < 14; ++n)
    {
        setup(&t);
        t.custom = 1;
        primary_block(&t, MM_CMTL);
        t.fail = n;
        o = NULL;
        ok = mm_custom_material_fetch(&t.cache, MM_CMTL, 1, &t.input, &o, &bytes, &t.s);
        if (ok)
        {
            assert(o->custom.count == 5 && o->custom.set == 3 && bytes == 56 + 10 * (int)sizeof(void *));
            assert(o->custom.materials[0] == &t.material && !o->custom.materials[1] && o->custom.models[2] == &t.model);
            assert(mm_custom_material_has_models(&o->custom));
            assert(mm_custom_material_model(&o->custom, 2) == &t.model);
            assert(mm_custom_material_handle(&o->custom, 0, &t.s) == &t.material);
            release(&t, o);
        }
        else if (t.ordered >= 3)
            assert(t.order[0] == 21 && t.order[1] == 10 && t.order[2] == 10);
        balanced(&t);
    }
    setup(&t);
    o = (Object *)create(&t, MM_MATERIAL);
    t.header_size = 19;
    assert(!mm_material_read(o, &t.cache, 3, 1, NULL, &t.shade, &t.s) && !o->material.handle);
    release(&t, o);
    balanced(&t);
    setup(&t);
    o = (Object *)create(&t, MM_MATERIAL);
    t.texture_child = t.transform_child = 1;
    t.transform_size = 27;
    assert(!mm_material_read(o, &t.cache, 3, 1, NULL, &t.shade, &t.s) && !o->material.handle && !t.texture.refs.count);
    release(&t, o);
    balanced(&t);
    setup(&t);
    o = (Object *)create(&t, MM_MATERIAL);
    t.transform_child = 1;
    t.transform_size = 1;
    assert(mm_material_read(o, &t.cache, 3, 1, NULL, &t.shade, &t.s) && !t.payload_handles && !t.cache.count);
    release(&t, o);
    balanced(&t);
    setup(&t);
    o = (Object *)create(&t, MM_CUSTOM_MATERIAL);
    t.custom = 1;
    t.custom_size = 9;
    assert(!mm_custom_material_read(o, &t.cache, MM_CMTL, 1, NULL, &t.s) &&
           t.diagnostic == MM_MATERIAL_BAD_CUSTOM_SIZE + 1);
    release(&t, o);
    balanced(&t);
}
static void construction_and_writes(void)
{
    Test t;
    Object *o;
    void *input[2];
    int n;
    MMU32 number;
    for (n = 0; n < 5; ++n)
    {
        setup(&t);
        input[0] = input[1] = &t.material;
        t.fail = n;
        o = (Object *)mm_custom_material_new(-3, 2, input, &t.s);
        if (o)
        {
            assert(o->custom.set == -3 && t.material.refs.count == 2);
            release(&t, o);
        }
        balanced(&t);
    }
    setup(&t);
    assert(!mm_custom_material_new(0, 2147483647, NULL, &t.s));
    balanced(&t);
    for (n = 0; n < 4; ++n)
    {
        setup(&t);
        o = (Object *)mm_material_new(-1, -1, &t.s);
        assert(o);
        assert(((Handle *)o->material.handle)->d.ambient == 6553 && ((Handle *)o->material.handle)->d.index_base == 15);
        mm_reference_add(&t.texture.refs);
        map(&t, o->material.handle, &t.texture_view, NULL);
        t.step = 0;
        t.fail = n;
        number = 99;
        if (mm_material_write(&o->material, &t, 3, &number, &t.s))
            assert(number == 10 && t.deleted == 0);
        else if (n == 2)
            assert(t.deleted == 1 && t.deletes[0] == 10);
        else if (n == 3)
            assert(t.deleted == 2 && t.deletes[0] == 11 && t.deletes[1] == 10);
        release(&t, o);
        balanced(&t);
    }
}
static void appearance_writes(void)
{
    static const MMI32 mapping[6] = {-65536, 65536, 0, 257, -12, 393216};
    static const int deletions[] = {0, 0, 1, 2, 1, 2};
    Test t;
    Object *o;
    MMMaterialDescription appearance = {0}, decoded;
    MMU32 number;
    int big, n;
    appearance.colour = 0x102030;
    appearance.ambient = 62000;
    appearance.diffuse = 17;
    appearance.specular = 24000;
    appearance.power = 24576;
    appearance.index_base = 10;
    appearance.index_range = 30;
    for (big = 0; big < 2; ++big)
        for (n = 0; n <= 5; ++n)
        {
            setup(&t);
            t.s.host_big_endian = big;
            o = (Object *)mm_material_new(-1, -1, &t.s);
            assert(o);
            mm_reference_add(&t.texture.refs);
            map(&t, o->material.handle, &t.texture_view, NULL);
            t.step = 0;
            t.fail = n;
            number = 99;
            assert(mm_material_write_appearance(&o->material, &t, 3, &number, &appearance, mapping, &t.s) == !n);
            assert(t.deleted == deletions[n]);
            if (n <= 1)
                assert(number == (n ? 99U : 10U));
            if (n > 1)
                assert(t.deletes[t.deleted - 1] == 10);
            if (n == 3 || n == 5)
                assert(t.deletes[0] == (n == 3 ? 11 : 12));
            if (!n)
            {
                assert(mm_material_header_read(t.written, big, 0x0303, &decoded));
                assert(decoded.colour == appearance.colour && decoded.power == appearance.power &&
                       decoded.ambient == appearance.ambient && decoded.diffuse == appearance.diffuse &&
                       decoded.specular == appearance.specular && decoded.index_base == 10 && decoded.index_range == 30);
                assert(t.written_transform[big ? 1 : 0] == 1 && t.written_transform[big ? 0 : 1] == 0);
                assert(t.written_transform[big ? 4 : 7] == 255 && t.written_transform[big ? 7 : 4] == 0);
                assert(t.written_transform[big ? 25 : 26] == 6);
                assert(((Handle *)o->material.handle)->d.ambient == 6553);
            }
            release(&t, o);
            balanced(&t);
        }
}
static void imports(void)
{
    Test t;
    Object *o;
    int bitmap, n;
    void *raw;
    for (bitmap = 0; bitmap < 2; ++bitmap)
        for (n = 0; n < 7; ++n)
        {
            setup(&t);
            t.fail = n;
            o = (Object *)mm_material_import(&t, NULL, bitmap, &t.shade, &t.s);
            if (o)
            {
                raw = ((Handle *)o->material.handle)->raw;
                release(&t, o);
                if (raw)
                    free_pixelmap(&t, raw);
            }
            balanced(&t);
        }
    setup(&t);
    assert(!mm_material_shade_set(&t.shade, NULL, &t.s) && t.shading.refs.count == 1);
    mm_reference_add(&t.shading.refs);
    assert(mm_material_shade_set(&t.shade, &t.shading, &t.s) && t.shading.refs.count == 1);
    mm_material_shade_dispose(&t.shade, &t.s);
    assert(!t.shade.texture && !t.shading.refs.count);
    balanced(&t);
}
int main(void)
{
    codecs();
    loads();
    costume_slots();
    construction_and_writes();
    appearance_writes();
    imports();
    puts("C89 material resources: wire codecs, partial owners, factories, imports, writes and failure cleanup passed.");
    return 0;
}
