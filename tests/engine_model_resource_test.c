#include "three_mm/model_resource.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fixture
{
    ThreeMMModelResource owner;
    ThreeMMModelDescription description;
    unsigned char block[144], vertices[64], written[144];
    ThreeMMModelFace face;
    ThreeMMI32 points[2][3];
    int size, fail, step, objects, models, memory, adds, removes, invalid, warnings, bad;
    int components, published, bound, reads, prepared, flags, identified, geometry;
} Fixture;
static int next(Fixture *f)
{
    return ++f->step != f->fail;
}
static ThreeMMModelResource *state(void *p, void *object)
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
    three_mm_model_init(&f->owner);
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
static int unpack(void *p, void *b)
{
    assert(p == b);
    return next((Fixture *)p);
}
static ThreeMMI32 size(void *p, void *b, int packed)
{
    Fixture *f = (Fixture *)p;
    assert(p == b);
    return packed ? 7 : f->size;
}
static int read_bytes(void *p, void *b, void *out, ThreeMMI32 bytes, ThreeMMI32 at)
{
    Fixture *f = (Fixture *)p;
    assert(p == b && at >= 0 && at + bytes <= f->size);
    ++f->reads;
    if (!next(f))
        return 0;
    if (bytes)
        memcpy(out, f->block + at, (size_t)bytes);
    return 1;
}
static int allocate(void *p, ThreeMMI32 bytes, int clear, void **out)
{
    Fixture *f = (Fixture *)p;
    *out = NULL;
    if (!next(f))
        return 0;
    *out = clear ? calloc(1, (size_t)bytes) : malloc((size_t)bytes);
    assert(*out);
    ++f->memory;
    return 1;
}
static void free_memory(void *p, void **out)
{
    Fixture *f = (Fixture *)p;
    if (*out)
    {
        free(*out);
        *out = NULL;
        --f->memory;
    }
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
    assert(code == THREE_MM_MODEL_BAD_ORDER || code == THREE_MM_MODEL_BAD_COUNT);
    ++f->bad;
}
static void warning(void *p)
{
    ++((Fixture *)p)->warnings;
}
static void *allocate_model(void *p, ThreeMMI32 vertices, ThreeMMI32 faces)
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
static void copy_input(void *p, void *h, const void *v, const void *faces, ThreeMMI32 nv, ThreeMMI32 nf)
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
static int component(void *p, void *h, int kind, ThreeMMI32 bytes)
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
static void set_face(void *p, void *h, int prepared, ThreeMMI32 index, const ThreeMMModelFace *face)
{
    Fixture *f = (Fixture *)p;
    (void)prepared;
    assert(h == &f->description && index == 0);
    f->face = *face;
}
static void get_face(void *p, void *h, ThreeMMI32 index, ThreeMMModelFace *face)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && index == 0);
    *face = f->face;
}
static void publish(void *p, void *h, const ThreeMMModelDescription *d, unsigned flag, ThreeMMI32 vg, ThreeMMI32 fg)
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
static void describe(void *p, void *h, ThreeMMModelDescription *d)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description);
    *d = f->description;
}
static void geometry(void *p, void *h, const ThreeMMModelDescription *d)
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description);
    f->description = *d;
    ++f->geometry;
}
static int put(void *p, void *file, ThreeMMU32 type, ThreeMMU32 number, const void *data, ThreeMMI32 bytes)
{
    Fixture *f = (Fixture *)p;
    assert(file == f && type == 3 && number == 7 && bytes == 144);
    if (!next(f))
        return 0;
    memcpy(f->written, data, (size_t)bytes);
    return 1;
}
static void position(void *p, void *h, ThreeMMI32 i, ThreeMMI32 out[3])
{
    Fixture *f = (Fixture *)p;
    assert(h == &f->description && i < 2);
    memcpy(out, f->points[i], sizeof(f->points[i]));
}
static void set_position(void *p, void *h, ThreeMMI32 i, const ThreeMMI32 in[3])
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
    ThreeMMModelFace face;
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
    three_mm_model_face_write(f->block + 112, big, &face);
    /* Discard input material and implicit padding, preserve explicit _pad0. */
    f->block[124] = 19;
    f->block[138] = 0x73;
    f->block[139] = 0x52;
    for (i = 0; i < 64; ++i)
        f->block[48 + i] = (unsigned char)(i * 3 + 1);
}
static void load_tests(ThreeMMModelServices *s)
{
    Fixture *f = (Fixture *)s->context;
    int big, prepared, fail, ok, steps;
    ThreeMMI32 bytes;
    void *out;
    ThreeMMModelDescription d;
    int bad;
    for (big = 0; big < 2; ++big)
        for (prepared = 0; prepared < 2; ++prepared)
        {
            s->host_big_endian = big;
            init(f, big, prepared);
            assert(three_mm_model_fetch(f, &out, &bytes, s));
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
                ok = three_mm_model_fetch(f, &out, &bytes, s);
                assert(!ok && f->invalid == 2 && !f->objects && !f->models && !f->memory);
            }
            init(f, big, prepared);
            assert(three_mm_model_fetch(f, NULL, &bytes, s) && bytes == 7 && !f->step);
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
            assert(three_mm_model_fetch(f, &out, &bytes, s) && f->face.normal[0] == -32768 &&
                   !memcmp(f->vertices, f->block + 48, 64));
            object_release(f, f);
        }
    s->host_big_endian = 0;
    init(f, 0, 1);
    for (fail = 0; fail < 144; ++fail)
    {
        f->size = fail;
        three_mm_model_init(&f->owner);
        f->objects = 1;
        assert(!three_mm_model_read(f, f, s));
        object_release(f, f);
        assert(!f->models && !f->memory);
    }
    init(f, 0, 1);
    f->block[4] = 0xff;
    f->block[5] = 0xff;
    assert(!three_mm_model_header(f->block, 0, &d, &bad) && !bad && d.vertices == -1);
    f->objects = 1;
    assert(!three_mm_model_read(f, f, s) && !f->owner.handle && f->bad == 1);
    object_release(f, f);
    init(f, 0, 1);
    f->block[0] = 2;
    f->objects = 1;
    assert(three_mm_model_read(f, f, s) && f->bad == 1);
    object_release(f, f);
}
static void author_tests(ThreeMMModelServices *s)
{
    Fixture *f = (Fixture *)s->context;
    ThreeMMModelFace face;
    ThreeMMModelDescription d;
    int big, bad, fail;
    void *object;
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
        three_mm_model_face_read(f->block + 112, big, &f->face);
        memcpy(f->vertices, f->block + 48, 64);
        assert(three_mm_model_write(&f->owner, f, 3, 7, s) && !f->memory);
        assert(three_mm_model_header(f->written, big, &d, &bad) && !bad && d.vertices == 2 && d.faces == 1 &&
               d.radius == 7 && d.bounds.min[0] == -3);
        assert(!memcmp(f->written + 48, f->vertices, 64));
        three_mm_model_face_read(f->written + 112, big, &face);
        assert(!memcmp(&face, &f->face, sizeof(face)));
        assert(!f->written[124] && !f->written[125] && !f->written[126] && !f->written[127] && !f->written[138] &&
               !f->written[139]);
        for (fail = 1; fail <= 2; ++fail)
        {
            f->step = 0;
            f->fail = fail;
            f->warnings = 0;
            assert(!three_mm_model_write(&f->owner, f, 3, 7, s) && f->warnings == 1 && !f->memory);
        }
    }
    s->host_big_endian = 0;
    for (fail = 0; fail < 4; ++fail)
    {
        init(f, 0, 0);
        f->fail = fail;
        object = three_mm_model_new(2, f->vertices, 1, &f->face, s);
        if (object)
        {
            assert(f->adds == 1 && f->identified == 1);
            object_release(f, f);
        }
        assert(!f->objects && !f->models);
        init(f, 0, 0);
        f->fail = fail;
        object = three_mm_model_dat(f, s);
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
    three_mm_model_dispose(&f->owner, s);
    three_mm_model_dispose(&f->owner, s);
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
    three_mm_model_adjust_glyph(&f->owner, s);
    assert(f->points[0][0] == 11 && f->points[0][1] == 11 && f->points[0][2] == 14);
    assert(f->description.bounds.min[0] == 1 && f->description.bounds.max[0] == 0 && f->description.pivot[2] == 2);
    assert(three_mm_model_extent(&f->owner, 0, s) == -1);
    init(f, 0, 0);
    assert(!three_mm_model_new(-1, f->vertices, 1, &f->face, s) && !f->step);
    assert(!three_mm_model_new(65535L, f->vertices, 1, &f->face, s) && !f->step);
    f->owner.handle = &f->description;
    f->description.bounds.min[0] = -655;
    f->description.bounds.max[0] = 655;
    three_mm_model_adjust_glyph(&f->owner, s);
    assert(f->description.bounds.max[0] == 0);
    f->description.bounds.min[0] = -656;
    f->description.bounds.max[0] = 656;
    three_mm_model_adjust_glyph(&f->owner, s);
    assert(f->description.bounds.max[0] == 33424);
}
typedef struct Lighting
{
    int fail, step, live, enabled, added, rendering, lit, diagnostics;
    int nodes[16], next;
} Lighting;
static void *light_create(void *p, int kind, const ThreeMMModelLightingSpec *spec)
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
    if (kind == THREE_MM_MODEL_LIGHT_WORLD)
    {
        for (i = 0; i < l->next; ++i)
            if (l->nodes[i] <= THREE_MM_MODEL_LIGHT_ACTOR)
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
    ThreeMMModelLightingServices s;
    ThreeMMModelResource m;
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
        assert(three_mm_model_prelight(&m, count, &s));
        total = l.step;
        assert(!l.live && !l.enabled && !l.added && l.lit == 1 && l.diagnostics == (count != 1));
        for (fail = 1; fail <= total; ++fail)
        {
            memset(&l, 0, sizeof(l));
            l.fail = fail;
            assert(!three_mm_model_prelight(&m, count, &s) && !l.live && !l.enabled && !l.added && !l.lit);
        }
    }
    memset(&l, 0, sizeof(l));
    assert(!three_mm_model_prelight(&m, 0, &s) && !three_mm_model_prelight(&m, 10, &s) && !l.step);
}
int main(void)
{
    Fixture f;
    ThreeMMModelServices s;
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
    s.unpack = unpack;
    s.size = size;
    s.read = read_bytes;
    s.allocate = allocate;
    s.free_memory = free_memory;
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
    s.put = put;
    s.position = position;
    s.set_position = set_position;
    s.path = path;
    s.load_dat = load_dat;
    s.or_flags = flags;
    s.prepare = prepare;
    load_tests(&s);
    author_tests(&s);
    lighting();
    puts("C89 model resources: wire codecs, factories, partial ownership, glyphs, saves and prelight cleanup passed.");
    return 0;
}
