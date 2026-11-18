#include "mm/material_resource.h"
#include "mm/template_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Ref
{
    int kind, refs;
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
    int step, fail, objects, handles, arrays, raw, closed, diagnostic, order[128], ordered;
    int texture_child, transform_child, custom, transform, bad_model_target, adds, deletes[4], deleted;
    unsigned char header[20], transform_header[28], custom_header[8], written[20];
    MMI32 header_size, transform_size, custom_size;
    Ref texture, material, model, shading;
} Test;
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
    o->ref.refs = 1;
    ++t->objects;
    return o;
}
static void retain(void *p, void *o)
{
    (void)p;
    ++((Ref *)o)->refs;
}
static void release(void *p, void *v)
{
    Test *t = test(p);
    Ref *r = (Ref *)v;
    assert(r && r->refs > 0);
    if (--r->refs)
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
static void *file(void *p, void *cache)
{
    (void)cache;
    return p;
}
static void *find(void *p, void *f, MMU32 type, MMU32 number)
{
    Test *t = test(p);
    (void)f;
    (void)number;
    if (fail(t))
        return NULL;
    t->transform = type == 2;
    return p;
}
static int unpack(void *p, void *b)
{
    (void)b;
    return !fail(test(p));
}
static MMI32 size(void *p, void *b)
{
    Test *t = test(p);
    (void)b;
    return t->transform ? t->transform_size : t->custom ? t->custom_size : t->header_size;
}
static int read(void *p, void *b, void *out, MMI32 n)
{
    Test *t = test(p);
    (void)b;
    if (fail(t))
        return 0;
    memcpy(out, t->transform ? t->transform_header : t->custom ? t->custom_header : t->header, (size_t)n);
    return 1;
}
static void close_block(void *p)
{
    ++test(p)->closed;
}
static int child(void *p, void *f, MMU32 type, MMU32 n, MMU32 id, MMU32 wanted,
                 MMMaterialChild *out)
{
    Test *t = test(p);
    int found;
    (void)f;
    (void)type;
    (void)n;
    found = id == 0 && ((wanted == 1 && t->texture_child) || (wanted == 2 && t->transform_child));
    if (found)
    {
        out->type = wanted;
        out->number = 7;
        out->id = id;
    }
    return found;
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
static void costume_chunks(void *p, void *f, MMChunkAccess *out)
{
    assert(p == f);
    out->context = p;
    out->count = 3;
    out->entry = costume_entry;
    out->child = costume_child;
}
static void *fetch(void *p, void *c, const MMMaterialChild *child_, int kind)
{
    Test *t = test(p);
    Ref *r;
    (void)c;
    if (t->custom)
        assert(child_->number == 7 &&
               (kind == MM_MATERIAL ? child_->id == 0 && child_->type == MM_MATERIAL_CHUNK
                                     : child_->id == 2 && child_->type == MM_TEMPLATE_BMDL));
    if (fail(t))
        return NULL;
    r = kind == MM_MATERIAL_TEXTURE ? &t->texture : kind == MM_MATERIAL_MODEL ? &t->model : &t->material;
    ++r->refs;
    return r;
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
static void map(void *p, void *h, void *t, void *raw, void *shade)
{
    (void)p;
    (void)shade;
    ((Handle *)h)->texture = (Ref *)t;
    ((Handle *)h)->raw = raw;
}
static MMI32 height(void *p, void *t)
{
    assert(t == &test(p)->shading);
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
    (void)type;
    assert(bytes == 20);
    if (fail(t))
        return 0;
    memcpy(t->written, data, 20);
    *number = 10;
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
    assert(n == 10 && c->id == 0 && c->number == 11);
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
static void *read_texture(void *p, void *f, MMU32 type, MMU32 number)
{
    Test *t = test(p);
    (void)f;
    (void)type;
    (void)number;
    if (fail(t))
        return NULL;
    ++t->shading.refs;
    return &t->shading;
}
static void *read_bitmap(void *p, void *filename, void *palette)
{
    Test *t = test(p);
    (void)filename;
    (void)palette;
    if (fail(t))
        return NULL;
    ++t->texture.refs;
    return &t->texture;
}
static void *read_pixelmap(void *p, void *filename)
{
    Test *t = test(p);
    void *raw;
    (void)filename;
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
    t->s.file = file;
    t->s.find = find;
    t->s.unpack = unpack;
    t->s.size = size;
    t->s.read = read;
    t->s.close_block = close_block;
    t->s.child = child;
    t->s.chunks = costume_chunks;
    t->s.fetch = fetch;
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
    t->s.map = map;
    t->s.height = height;
    t->s.transform = transform;
    t->s.add_chunk = add_chunk;
    t->s.write_texture = write_texture;
    t->s.adopt = adopt;
    t->s.delete_chunk = delete_chunk;
    t->s.read_texture = read_texture;
    t->s.read_bitmap = read_bitmap;
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
    t->shading.refs = 1;
    t->shade.texture = &t->shading;
}
static void balanced(Test *t)
{
    assert(!t->objects && !t->handles && !t->arrays && !t->raw && !t->texture.refs && !t->material.refs &&
           !t->model.refs);
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
    costume_chunks(&t, &t, &graph);
    assert(mm_custom_material_part(&graph, 1, 0, &part) == MM_CHUNK_OK);
    assert(part.material.type == MM_MATERIAL_CHUNK && part.material.number == 7 && !part.model.type);
    assert(mm_custom_material_part(&graph, 1, 4, &part) == MM_CHUNK_OK);
    assert(!part.material.type && !part.model.type);
    assert(mm_custom_material_part(&graph, 1, 2, &part) == MM_CHUNK_OK);
    assert(!part.material.type && part.model.type == MM_TEMPLATE_BMDL && part.model.number == 7);
    t.bad_model_target = 1;
    assert(mm_custom_material_part(&graph, 1, 2, &part) == MM_CHUNK_BAD_FILE);
    assert(!part.material.type && part.model.number == 7);
    assert(!mm_custom_material_fetch(&t, MM_CMTL, 1, &o, &bytes, &t.s));
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
        t.fail = n;
        o = NULL;
        ok = mm_material_fetch(&t, 3, 1, &o, &bytes, &t.shade, &t.s);
        if (ok)
        {
            Handle *h = (Handle *)o->material.handle;
            assert(bytes == 32 && h->texture == &t.texture && h->d.index_range == 7 && h->matrix[0] == 7);
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
        t.fail = n;
        o = NULL;
        ok = mm_custom_material_fetch(&t, MM_CMTL, 1, &o, &bytes, &t.s);
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
    assert(!mm_material_read(o, &t, 3, 1, &t.shade, &t.s) && !o->material.handle);
    release(&t, o);
    balanced(&t);
    setup(&t);
    o = (Object *)create(&t, MM_MATERIAL);
    t.texture_child = t.transform_child = 1;
    t.transform_size = 27;
    assert(!mm_material_read(o, &t, 3, 1, &t.shade, &t.s) && !o->material.handle && !t.texture.refs);
    release(&t, o);
    balanced(&t);
    setup(&t);
    o = (Object *)create(&t, MM_CUSTOM_MATERIAL);
    t.custom = 1;
    t.custom_size = 9;
    assert(!mm_custom_material_read(o, &t, MM_CMTL, 1, &t.s) && t.diagnostic == MM_MATERIAL_BAD_CUSTOM_SIZE + 1);
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
            assert(o->custom.set == -3 && t.material.refs == 2);
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
        ++t.texture.refs;
        map(&t, o->material.handle, &t.texture, NULL, NULL);
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
    t.fail = 1;
    assert(!mm_material_shade_set(&t.shade, &t, 1, 1, &t.s) && t.shading.refs == 1);
    t.fail = 0;
    assert(mm_material_shade_set(&t.shade, &t, 1, 1, &t.s) && t.shading.refs == 1);
    mm_material_shade_dispose(&t.shade, &t.s);
    assert(!t.shade.texture && !t.shading.refs);
}
int main(void)
{
    codecs();
    loads();
    costume_slots();
    construction_and_writes();
    imports();
    puts("C89 material resources: wire codecs, partial owners, factories, imports, writes and failure cleanup passed.");
    return 0;
}
