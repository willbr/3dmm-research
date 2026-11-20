#include "mm/text_store.h"
#include "mm/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "text persistence failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Token
{
    int refs;
    MMI32 width, count;
} Token;
typedef struct Host
{
    MMRichTextReadServices read;
    MMRichTextWriteServices write;
    Token old[3], fresh[3], destination;
    unsigned char header[64], marker[2], written[36];
    unsigned char payload[4];
    MMU32 length;
    int calls, fail_at, failure, own_failure, font_empty, missing_args, big;
    int root, removed, children, active, releases;
    char log[256];
    unsigned used;
} Host;
static void event(Host *h, char c)
{
    CHECK(h->used < 255);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int step(Host *h, char c)
{
    event(h, c);
    return ++h->calls == h->fail_at ? h->failure : 1;
}
static void retain(void *v, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    CHECK(t == h->fresh && t->refs == 1);
    ++t->refs;
    event(h, '+');
}
static void release(void *v, MMU32 kind, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    unsigned i;
    CHECK(t->refs > 0);
    --t->refs;
    ++h->releases;
    if (t == &h->destination)
    {
        CHECK(kind == MM_TEXT_BYTES);
        event(h, 'z');
        return;
    }
    for (i = 0; i < 3; ++i)
        if (t == h->old + i)
        {
            event(h, (char)('X' + i));
            return;
        }
    for (i = 0; i < 3; ++i)
        if (t == h->fresh + i)
        {
            event(h, (char)('x' + i));
            return;
        }
    CHECK(0);
}
static MMI32 width(void *v, MMU32 kind, void *p)
{
    (void)v;
    CHECK(kind == MM_TEXT_PROPERTIES || kind == MM_TEXT_ARGUMENTS);
    return ((Token *)p)->width;
}
static int root_read(void *v, MMU32 type, MMU32 number, MMReadStream *out);
static int root_bytes(void *v, MMU32 offset, void *out, size_t n)
{
    Host *h = (Host *)v;
    int r = step(h, 'H');
    CHECK(offset == 0 && n == 16);
    if (r > 0)
        memcpy(out, h->header, n);
    return r > 0;
}
static int root_read(void *v, MMU32 type, MMU32 number, MMReadStream *out)
{
    Host *h = (Host *)v;
    int r = step(h, 'R');
    CHECK(type == MM_TEXT_ROOT && number == 9);
    if (r > 0)
    {
        out->context = h;
        out->length = h->length;
        out->read_at = root_bytes;
    }
    return r;
}
static int font_read(void *v, MMMovieTitle *font)
{
    Host *h = (Host *)v;
    int r = step(h, 'F');
    if (r > 0)
    {
        if (font->unit == 1)
            CHECK(mm_movie_title_set(font, "Arial", h->font_empty ? 0 : 5));
        else
        {
            const unsigned short name[] = {'A', 'r', 'i', 'a', 'l'};
            CHECK(mm_movie_title_set(font, name, h->font_empty ? 0 : 5));
        }
    }
    return r;
}
static int child(void *v, MMU32 type, MMU32 number, MMU32 kind, MMU32 *out)
{
    Host *h = (Host *)v;
    int r = step(h, kind == MM_TEXT_BYTES ? 'T' : kind == MM_TEXT_PROPERTIES ? 'P' : 'A');
    CHECK(type == MM_TEXT_ROOT && number == 9);
    *out = 100;
    if (kind == MM_TEXT_ARGUMENTS && h->missing_args)
        return 0;
    return r;
}
static int location(void *v, MMU32 type, MMU32 number, MMTextLocation *out)
{
    Host *h = (Host *)v;
    int r = step(h, 'L');
    CHECK(type == MM_TEXT_BYTES && number == 100);
    out->file = h->fresh;
    out->offset = 40;
    out->bytes = 6;
    return r;
}
static int text_read(void *v, const MMTextLocation *p, void *out, MMU32 n)
{
    Host *h = (Host *)v;
    int r = step(h, 'E');
    CHECK(p->file == h->fresh && p->offset == 40 && n == 2);
    if (r > 0)
        memcpy(out, h->marker, 2);
    return r;
}
static int group_read(void *v, MMU32 kind, MMU32 number, void **out, int *order, int *system)
{
    Host *h = (Host *)v;
    int index = kind == MM_TEXT_PROPERTIES ? 1 : 2;
    int r = step(h, index == 1 ? 'G' : 'B');
    CHECK(number == 100);
    if (r > 0 || h->own_failure)
    {
        *out = h->fresh + index;
        CHECK(!h->fresh[index].refs);
        h->fresh[index].refs = 1;
        *order = 256;
        *system = 0x202;
    }
    return r;
}
static MMU32 font_bytes(void *v, const MMMovieTitle *font)
{
    (void)v;
    CHECK(font->unit == 1 || font->unit == 2);
    return 8;
}
static int add_root(void *v, MMU32 bytes, MMU32 *out)
{
    Host *h = (Host *)v;
    int r = step(h, 'n');
    CHECK(bytes == 24);
    *out = 41;
    if (r > 0)
        h->root = 1;
    h->active = 0;
    return r;
}
static int write_bytes(void *v, MMU32 offset, const void *bytes, MMU32 n)
{
    Host *h = (Host *)v;
    int r = step(h, offset == 2 ? 'b' : h->active ? 'm' : 'w');
    if (offset == 2)
    {
        CHECK(h->active == 1 && n == sizeof(h->payload));
        if (r > 0)
            memcpy(h->payload, bytes, n);
        return r;
    }
    CHECK(offset == 0 && n == (h->active ? 2U : 16U));
    if (r > 0)
        memcpy(h->written, bytes, n);
    return r;
}
static int font_write(void *v, const MMMovieTitle *font)
{
    (void)font;
    return step((Host *)v, 'f');
}
static void remove_root(void *v, MMU32 number)
{
    Host *h = (Host *)v;
    CHECK(number == 41 && h->root);
    h->root = h->children = 0;
    ++h->removed;
    event(h, '!');
}
static int add_child(void *v, MMU32 root, MMU32 kind, MMU32 bytes)
{
    Host *h = (Host *)v;
    int r = step(h, kind == MM_TEXT_BYTES ? 't' : kind == MM_TEXT_PROPERTIES ? 'p' : 'a');
    CHECK(root == 41 && h->root && (kind == MM_TEXT_BYTES ? (bytes == 6 || bytes == 2) : bytes == 20));
    if (r > 0)
    {
        ++h->children;
        h->active = kind == MM_TEXT_BYTES ? 1 : 2;
    }
    return r;
}
static int destination(void *v, MMTextLocation *out)
{
    Host *h = (Host *)v;
    int r = step(h, 'd');
    if (r > 0 || h->own_failure)
    {
        out->file = &h->destination;
        out->offset = 80;
        out->bytes = 6;
        ++h->destination.refs;
    }
    return r;
}
static int copy(void *v, const MMTextLocation *from, const MMTextLocation *to)
{
    Host *h = (Host *)v;
    CHECK(from->file == h->old && from->bytes == 4 && to->offset == 82 && to->bytes == 4);
    return step(h, 'c');
}
static MMU32 group_bytes(void *v, MMU32 kind, void *p)
{
    (void)v;
    (void)kind;
    (void)p;
    return 20;
}
static MMI32 count(void *v, void *p)
{
    (void)v;
    return ((Token *)p)->count;
}
static int group_write(void *v, MMU32 kind, void *p, int order, int system)
{
    Host *h = (Host *)v;
    CHECK(p == h->old + (kind == MM_TEXT_PROPERTIES ? 1 : 2) && order == 256 && system == 0x202);
    return step(h, kind == MM_TEXT_PROPERTIES ? 'g' : 'j');
}
static void setup(Host *h, MMRichTextState *s, unsigned unit, int big)
{
    MMRichTextObjects objects;
    unsigned i;
    memset(h, 0, sizeof(*h));
    CHECK(mm_rich_text_init(s, unit, 0x303));
    h->big = big;
    s->defaults.width = 240;
    s->defaults.height = 16;
    s->defaults.background = 0xfe001122U;
    CHECK(mm_rich_text_encode(&s->defaults, big, h->header));
    h->length = 24;
    h->marker[0] = 0x34;
    h->marker[1] = 0x12;
    s->text.file = h->old;
    s->text.offset = 20;
    s->text.bytes = 4;
    s->properties = h->old + 1;
    s->arguments = h->old + 2;
    s->property_order = s->argument_order = 256;
    s->property_system = s->argument_system = 0x202;
    for (i = 0; i < 3; ++i)
    {
        h->old[i].refs = 1;
        h->old[i].count = h->fresh[i].count = 1;
        h->old[i].width = h->fresh[i].width = i == 1 ? 8 : 4;
    }
    h->fresh[0].refs = 1;
    objects.context = h;
    objects.retain_file = retain;
    objects.release = release;
    objects.width = width;
    h->read.context = h;
    h->read.objects = objects;
    h->read.root = root_read;
    h->read.font = font_read;
    h->read.child = child;
    h->read.location = location;
    h->read.read_text = text_read;
    h->read.group = group_read;
    h->write.context = h;
    h->write.objects = objects;
    h->write.font_bytes = font_bytes;
    h->write.add_root = add_root;
    h->write.write = write_bytes;
    h->write.font = font_write;
    h->write.remove = remove_root;
    h->write.add_child = add_child;
    h->write.destination = destination;
    h->write.copy = copy;
    h->write.group_bytes = group_bytes;
    h->write.count = count;
    h->write.group = group_write;
}
static void read_tests(void)
{
    Host h;
    MMRichTextState s, before;
    int n, i, f, big, r, own;
    unsigned unit;
    char effects[32];
    unsigned used;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
        {
            setup(&h, &s, unit, big);
            CHECK(mm_rich_text_read(&s, MM_TEXT_ROOT, 9, 0x303, big, &h.read) == 1);
            CHECK(s.text.file == h.fresh && s.text.offset == 42 && s.text.bytes == 4 &&
                  s.text_system == (big ? 0x3412 : 0x1234));
            CHECK(s.font.count == 5 && s.property_order == 256 && s.argument_system == 0x202 && strstr(h.log, "XYZ"));
            n = h.calls;
            used = 0;
            for (i = 0; h.log[i]; ++i)
                if (h.log[i] != '+' && h.log[i] != 'X' && h.log[i] != 'Y' && h.log[i] != 'Z')
                    effects[used++] = h.log[i];
            effects[used] = 0;
            mm_rich_text_dispose(&s, &h.read.objects);
            CHECK(h.fresh[0].refs == 1 && !h.fresh[1].refs && !h.fresh[2].refs && !s.text.file && !s.properties &&
                  !s.arguments);
            for (own = 0; own < 2; ++own)
                for (f = -2; f <= 0; ++f)
                    for (i = 1; i <= n; ++i)
                    {
                        setup(&h, &s, unit, big);
                        before = s;
                        h.fail_at = i;
                        h.failure = f;
                        h.own_failure = own;
                        r = mm_rich_text_read(&s, MM_TEXT_ROOT, 9, 0x303, big, &h.read);
                        if (!f && (effects[i - 1] == 'F' || effects[i - 1] == 'A'))
                        {
                            CHECK(r == 1);
                            if (effects[i - 1] == 'F')
                                CHECK(!s.font.count && s.defaults.font_system == 0x303);
                            if (effects[i - 1] == 'A')
                                CHECK(!s.arguments);
                            mm_rich_text_dispose(&s, &h.read.objects);
                        }
                        else
                        {
                            CHECK(r == (effects[i - 1] == 'H' ? 0 : f) && !memcmp(&s, &before, sizeof(s)));
                            CHECK(h.old[0].refs == 1 && h.old[1].refs == 1 && h.old[2].refs == 1);
                        }
                        CHECK(h.fresh[0].refs == 1 && !h.fresh[1].refs && !h.fresh[2].refs);
                    }
        }
    setup(&h, &s, 1, 0);
    h.font_empty = 1;
    CHECK(mm_rich_text_read(&s, MM_TEXT_ROOT, 9, 0x303, 0, &h.read) == 1 && !s.font.count);
    setup(&h, &s, 1, 0);
    h.fresh[1].width = 4;
    before = s;
    CHECK(!mm_rich_text_read(&s, MM_TEXT_ROOT, 9, 0x303, 0, &h.read) && !memcmp(&before, &s, sizeof(s)) &&
          !h.fresh[1].refs && h.fresh[0].refs == 1);
    setup(&h, &s, 1, 0);
    h.fresh[2].width = 8;
    CHECK(!mm_rich_text_read(&s, MM_TEXT_ROOT, 9, 0x303, 0, &h.read) && strstr(h.log, "xyz"));
    setup(&h, &s, 1, 0);
    h.length = 15;
    CHECK(!mm_rich_text_read(&s, MM_TEXT_ROOT, 9, 0x303, 0, &h.read) && h.calls == 1);
    setup(&h, &s, 1, 0);
    h.header[0] = 2;
    CHECK(!mm_rich_text_read(&s, MM_TEXT_ROOT, 9, 0x303, 0, &h.read) && h.calls == 2);
}
static void write_tests(void)
{
    Host h;
    MMRichTextState s;
    MMTextChunk out;
    int n, i, f, big, own, r;
    unsigned unit;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
        {
            setup(&h, &s, unit, big);
            out.type = 22;
            out.number = 33;
            CHECK(mm_rich_text_write(&s, &out, big, &h.write) == 1 && out.type == MM_TEXT_ROOT && out.number == 41 &&
                  h.children == 3 && !h.removed && !h.destination.refs);
            n = h.calls;
            CHECK(!strcmp(h.log, "nwftmdczpgaj"));
            for (own = 0; own < 2; ++own)
                for (f = -2; f <= 0; ++f)
                    for (i = 1; i <= n; ++i)
                    {
                        setup(&h, &s, unit, big);
                        h.fail_at = i;
                        h.failure = f;
                        h.own_failure = own;
                        out.type = 22;
                        out.number = 33;
                        r = mm_rich_text_write(&s, &out, big, &h.write);
                        CHECK(r == f && out.type == MM_TEXT_ROOT && out.number == MM_TEXT_NIL && !h.destination.refs &&
                              !h.root && !h.children && h.removed == (i != 1));
                        CHECK(h.old[0].refs == 1 && h.old[1].refs == 1 && h.old[2].refs == 1);
                        if (strchr(h.log, 'z'))
                            CHECK(strchr(h.log, 'z') < strchr(h.log, '!'));
                    }
        }
    for (i = 0; i < 7; ++i)
    {
        setup(&h, &s, 1, 0);
        out.type = 22;
        out.number = 33;
        if (i == 0)
            s.text.bytes = -1;
        if (i == 1)
            s.text.bytes = MM_TEXT_LIMIT - 1;
        if (i == 2)
            s.properties = 0;
        if (i == 3)
            h.old[1].width = 4;
        if (i == 4)
            h.old[2].width = 8;
        if (i == 5)
            s.defaults.height = 256;
        if (i == 6)
            s.defaults.width = MM_TEXT_LIMIT;
        CHECK(!mm_rich_text_write(&s, &out, 0, &h.write) && !h.calls && out.type == 22 && out.number == 33);
    }
    setup(&h, &s, 1, 0);
    s.arguments = 0;
    s.text.bytes = 0;
    CHECK(mm_rich_text_write(&s, &out, 0, &h.write) == 1 && h.children == 2 && !strchr(h.log, 'd') &&
          !strchr(h.log, 'a'));
    setup(&h, &s, 1, 0);
    h.old[2].count = 0;
    CHECK(mm_rich_text_write(&s, &out, 0, &h.write) == 1 && h.children == 2 && !strchr(h.log, 'a'));
}

static void encoded_tests(void)
{
    static const unsigned char words[] = {'A', 0, 13, 0};
    Host h;
    MMRichTextState state;
    MMTextChunk chunk;
    MMTextEncoded replacement = {words, 4, MM_TEXT_UTF16_LE};
    int big;
    for (big = 0; big < 2; ++big)
    {
        setup(&h, &state, 1, big);
        CHECK(mm_rich_text_write_encoded(&state, &replacement, &chunk, big, &h.write) == 1);
        CHECK(chunk.type == MM_TEXT_ROOT && chunk.number == 41 && h.children == 3);
        CHECK(!strcmp(h.log, "nwftmbpgaj") && !memcmp(h.payload, words, 4));
        CHECK(h.written[0] == 5 && h.written[1] == 5 && h.old[0].refs == 1);

        setup(&h, &state, 1, big);
        h.fail_at = 6;
        h.failure = -2;
        CHECK(mm_rich_text_write_encoded(&state, &replacement, &chunk, big, &h.write) == -2);
        CHECK(!strcmp(h.log, "nwftmb!") && chunk.number == MM_TEXT_NIL && h.removed == 1 && !h.children);
    }
    setup(&h, &state, 1, 0);
    replacement.bytes = 0;
    replacement.text = NULL;
    CHECK(mm_rich_text_write_encoded(&state, &replacement, &chunk, 0, &h.write) == 1);
    CHECK(!strcmp(h.log, "nwftmpgaj") && state.text.bytes == 4);
}
typedef struct Box
{
    MMTextBoxServices services;
    unsigned char bytes[37];
    MMU32 length;
    int calls, fail_at, failure;
} Box;
static int box_step(Box *h)
{
    return ++h->calls == h->fail_at ? h->failure : 1;
}
static int box_bytes(void *v, MMU32 offset, void *out, size_t n)
{
    Box *h = (Box *)v;
    int r = box_step(h);
    CHECK(!offset && n == 36);
    if (r > 0)
        memcpy(out, h->bytes, n);
    return r > 0;
}
static int box_read(void *v, MMU32 number, MMReadStream *out)
{
    Box *h = (Box *)v;
    int r = box_step(h);
    CHECK(number == 9);
    out->context = h;
    out->length = h->length;
    out->read_at = box_bytes;
    return r;
}
static int box_child(void *v, MMU32 number, MMU32 slot, MMTextChunk *out)
{
    Box *h = (Box *)v;
    int r = box_step(h);
    CHECK(number == 9 && slot == 7);
    out->type = MM_TEXT_ROOT;
    out->number = 12;
    return r;
}
static int box_put(void *v, MMU32 number, const unsigned char *bytes)
{
    Box *h = (Box *)v;
    CHECK(number == 9);
    memcpy(h->bytes, bytes, 36);
    return box_step(h);
}
static void codecs(void)
{
    Box h;
    MMTextBoxState s, out, before;
    MMTextChunk child;
    MMMemoryStream memory;
    MMRichTextDefaults d, decoded;
    unsigned char encoded[36], unchanged[36];
    int big, i, f;
    memset(&h, 0, sizeof(h));
    h.services.context = &h;
    h.services.read = box_read;
    h.services.child = box_child;
    h.services.put = box_put;
    memset(&s, 0, sizeof(s));
    s.first = -2;
    s.end = -8;
    s.left = -10;
    s.top = 20;
    s.right = 150;
    s.bottom = 75;
    s.story = 1;
    s.child = 7;
    for (big = 0; big < 2; ++big)
    {
        CHECK(mm_text_box_encode(&s, 0x1111, big, h.bytes));
        CHECK(h.bytes[big ? 1 : 0] == 1 && h.bytes[32] == 1 && !h.bytes[33] && !h.bytes[34] && !h.bytes[35]);
        h.length = 36;
        h.calls = 0;
        CHECK(mm_text_box_read(&out, 9, &child, &h.services) == 1 && out.current == -3 && out.end == -8 &&
              out.left == -10 && child.number == 12);
        for (f = -2; f <= 0; ++f)
            for (i = 1; i <= 3; ++i)
            {
                h.calls = 0;
                h.fail_at = i;
                h.failure = f;
                memset(&out, 0x77, sizeof(out));
                before = out;
                child.type = 22;
                child.number = 33;
                CHECK(mm_text_box_read(&out, 9, &child, &h.services) == (i == 2 ? 0 : f) &&
                      !memcmp(&before, &out, sizeof(out)) && child.type == 22 && child.number == 33);
            }
        h.fail_at = h.calls = 0;
        CHECK(mm_text_box_read(&out, 9, 0, &h.services) == 1 && h.calls == 2);
        CHECK(mm_text_box_write(&s, 9, 0x303, big, &h.services) == 1);
        CHECK(mm_memory_stream_init(&memory, h.bytes, 36));
        h.bytes[32] = 2;
        before = out;
        CHECK(!mm_text_box_decode(&memory.stream, &out) && !memcmp(&before, &out, sizeof(out)));
        h.bytes[32] = 1;
        memory.stream.length = 35;
        CHECK(!mm_text_box_decode(&memory.stream, &out));
        memory.stream.length = 37;
        CHECK(!mm_text_box_decode(&memory.stream, &out));
        s.first = (-2147483647L - 1);
        memset(encoded, 0x77, 36);
        memcpy(unchanged, encoded, 36);
        CHECK(!mm_text_box_encode(&s, 0x303, big, encoded) && !memcmp(encoded, unchanged, 36));
        s.first = -2;
        d.order = 256;
        d.font_system = -123;
        d.width = MM_TEXT_LIMIT - 1;
        d.height = 255;
        d.background = 0xfe001122U;
        CHECK(mm_rich_text_encode(&d, big, encoded));
        CHECK(mm_memory_stream_init(&memory, encoded, 17));
        CHECK(mm_rich_text_decode(&memory.stream, &decoded) && decoded.order == 1 && decoded.font_system == -123 &&
              decoded.background == d.background && decoded.width == d.width);
        d.height = 4;
        CHECK(mm_rich_text_encode(&d, big, encoded));
        d.height = 256;
        CHECK(!mm_rich_text_encode(&d, big, encoded));
        d.height = 3;
        CHECK(!mm_rich_text_encode(&d, big, encoded));
        d.height = 12;
        d.width = 0;
        CHECK(!mm_rich_text_encode(&d, big, encoded));
        d.width = MM_TEXT_LIMIT;
        CHECK(!mm_rich_text_encode(&d, big, encoded));
        memory.stream.length = 15;
        CHECK(!mm_rich_text_decode(&memory.stream, &decoded));
    }
}
int main(void)
{
    codecs();
    read_tests();
    write_tests();
    encoded_tests();
    puts("Stored text: both wire orders, transactional ownership, font fallback, output prefixes and exhaustive effect "
         "failures passed.");
    return 0;
}
