#include "three_mm/actor_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "actor data failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
static void sw16(unsigned char *p)
{
    unsigned char v = p[0];
    p[0] = p[1];
    p[1] = v;
}
static void sw32(unsigned char *p)
{
    unsigned char v = p[0];
    p[0] = p[3];
    p[3] = v;
    v = p[1];
    p[1] = p[2];
    p[2] = v;
}
static void words(void *p, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i)
        sw32((unsigned char *)p + i * 4);
}
typedef struct HeaderIO
{
    unsigned char bytes[44];
    ThreeMMU32 length;
    int reads, writes, fail_read, fail_write, fail_find;
} HeaderIO;
static int read_at(void *c, ThreeMMU32 offset, void *out, size_t bytes)
{
    HeaderIO *h = (HeaderIO *)c;
    ++h->reads;
    CHECK(offset + bytes <= 44);
    memcpy(out, h->bytes + offset, h->fail_read ? 3 : bytes);
    return !h->fail_read;
}
static int find_header(void *c, ThreeMMU32 number, ThreeMMReadStream *s)
{
    HeaderIO *h = (HeaderIO *)c;
    CHECK(number == 7);
    s->context = h;
    s->length = h->length;
    s->read_at = read_at;
    return !h->fail_find;
}
static int put_header(void *c, ThreeMMU32 number, const void *bytes, ThreeMMU32 size)
{
    HeaderIO *h = (HeaderIO *)c;
    CHECK(number == 7 && size == 44);
    ++h->writes;
    memcpy(h->bytes, bytes, 44);
    return !h->fail_write;
}
static void headers(void)
{
    HeaderIO h;
    ThreeMMActorHeader original, out, before;
    ThreeMMReadStream s;
    ThreeMMActorHeaderIO io = {NULL, find_header, put_header};
    unsigned char legacy[40];
    int other;
    ThreeMMI32 id;
    memset(&h, 0, sizeof(h));
    memset(&original, 0, sizeof(original));
    original.order = 1;
    original.system = 0xaaaa;
    original.translation[0] = 0x7fc01234UL;
    original.translation[1] = 0x80000000UL;
    original.translation[2] = 0xffffffffUL;
    original.actor_id = 17;
    original.first_frame = (-2147483647L - 1);
    original.last_frame = -9;
    original.source_id = -12;
    original.wire_pointer_pad = 0xdeadbeefUL;
    original.template_type = 0x544d504cUL;
    original.template_number = 99;
    io.context = &h;
    s.context = &h;
    s.read_at = read_at;
    for (other = 0; other < 2; ++other)
    {
        out = original;
        if (other)
        {
            sw16((unsigned char *)&out);
            sw16((unsigned char *)&out + 2);
            words((unsigned char *)&out + 4, 10);
        }
        memcpy(h.bytes, &out, 44);
        s.length = 44;
        CHECK(three_mm_actor_read_header(&s, &out) && !memcmp(&out, &original, 44));
        memcpy(legacy, h.bytes, 24);
        memcpy(legacy + 24, h.bytes + 28, 16);
        memcpy(h.bytes, legacy, 40);
        s.length = 40;
        CHECK(three_mm_actor_read_header(&s, &out));
        before = original;
        before.last_frame = 2147483647L;
        CHECK(!memcmp(&out, &before, 44));
    }
    out = original;
    out.order = 3;
    memcpy(legacy, &out, 24);
    memcpy(legacy + 24, (unsigned char *)&out + 28, 16);
    memcpy(h.bytes, legacy, 40);
    s.length = 40;
    CHECK(!three_mm_actor_read_header(&s, &out) && out.order == 3 && out.last_frame == original.source_id &&
          out.template_number == 99);
    memset(&out, 0xa5, 44);
    before = out;
    s.length = 20;
    h.reads = 0;
    CHECK(!three_mm_actor_read_header(&s, &out) && !h.reads && !memcmp(&out, &before, 44));
    s.length = 44;
    h.fail_read = 1;
    memcpy(h.bytes, &original, 44);
    CHECK(!three_mm_actor_read_header(&s, &out));
    CHECK(!memcmp(&out, &original, 3) && !memcmp((unsigned char *)&out + 3, (unsigned char *)&before + 3, 41));
    h.fail_read = 0;
    out = original;
    out.actor_id = 2147483647L;
    before = out;
    CHECK(!three_mm_actor_adjust_header_id(&out, 1) && !memcmp(&out, &before, 44));
    out.actor_id = (-2147483647L - 1);
    before = out;
    CHECK(!three_mm_actor_adjust_header_id(&out, -1) && !memcmp(&out, &before, 44));
    out.actor_id = 0;
    CHECK(three_mm_actor_adjust_header_id(&out, (-2147483647L - 1)) && out.actor_id == (-2147483647L - 1));
    out.actor_id = 2147483647L;
    CHECK(three_mm_actor_adjust_header_id(&out, (-2147483647L - 1)) && out.actor_id == -1);
    h.length = 44;
    memcpy(h.bytes, &original, 44);
    h.writes = 0;
    CHECK(three_mm_actor_adjust_id(7, 3, &io) && h.writes == 1);
    memcpy(&id, h.bytes + 16, 4);
    CHECK(id == 20);
    out = original;
    out.actor_id = 2147483647L;
    memcpy(h.bytes, &out, 44);
    h.writes = 0;
    CHECK(!three_mm_actor_adjust_id(7, 1, &io) && !h.writes);
    h.fail_find = 1;
    CHECK(!three_mm_actor_adjust_id(7, 0, &io) && !h.writes);
    h.fail_find = 0;
    h.fail_write = 1;
    CHECK(!three_mm_actor_write_header(7, &original, &io) && h.writes == 1);
}
typedef struct Route
{
    ThreeMMU32 width, count, bits[8];
    int data_calls, released, count_calls;
} Route;
static ThreeMMU32 route_width(void *p)
{
    return ((Route *)p)->width;
}
static ThreeMMU32 route_count(void *p)
{
    ++((Route *)p)->count_calls;
    return ((Route *)p)->count;
}
static void *route_data(void *p)
{
    Route *r = (Route *)p;
    CHECK(r->count);
    ++r->data_calls;
    return r->bits;
}
static void release_route(void *p)
{
    ++((Route *)p)->released;
}
static void routes(void)
{
    Route r;
    ThreeMMU32 before[8];
    ThreeMMActorRouteServices s = {route_width, route_count, route_data, release_route};
    unsigned i;
    memset(&r, 0, sizeof(r));
    r.width = 16;
    CHECK(three_mm_actor_finish_route(NULL, 0, &s) == NULL);
    CHECK(three_mm_actor_finish_route(&r, THREE_MM_ACTOR_OTHER, &s) == &r && !r.data_calls);
    r.count = 2;
    for (i = 0; i < 8; ++i)
        r.bits[i] = 0x12345678UL + i;
    memcpy(before, r.bits, 32);
    r.count_calls = 0;
    CHECK(three_mm_actor_finish_route(&r, THREE_MM_ACTOR_NATIVE, &s) == &r && !r.data_calls && !r.count_calls);
    CHECK(three_mm_actor_finish_route(&r, THREE_MM_ACTOR_OTHER, &s) == &r && r.data_calls == 1 &&
          memcmp(before, r.bits, 32));
    CHECK(three_mm_actor_finish_route(&r, THREE_MM_ACTOR_OTHER, &s) == &r && !memcmp(before, r.bits, 32));
    r.width = 12;
    CHECK(!three_mm_actor_finish_route(&r, 0, &s) && r.released == 1);
    r.width = 16;
    CHECK(!three_mm_actor_finish_route(&r, THREE_MM_ACTOR_INVALID_ORDER, &s) && r.released == 2);
}
typedef struct Host Host;
typedef struct Group
{
    Host *host;
    int wire;
    ThreeMMU32 width, count;
    ThreeMMI32 sizes[13];
    unsigned char fixed[13][20], data[13][64];
} Group;
struct Host
{
    int live, new_fail, fail_at, inserts, allocations;
    char log[64];
    int used, inspect_native;
};
static void event(Host *h, char c)
{
    CHECK(h->used < 63);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static Group *new_group_raw(Host *h, int wire)
{
    Group *g = (Group *)calloc(1, sizeof(*g));
    CHECK(g != NULL);
    g->host = h;
    g->wire = wire;
    g->width = 20;
    ++h->live;
    return g;
}
static ThreeMMU32 fixed_width(void *g)
{
    return ((Group *)g)->width;
}
static ThreeMMU32 group_count(void *g)
{
    return ((Group *)g)->count;
}
static void *fixed(void *p, ThreeMMU32 i)
{
    Group *g = (Group *)p;
    CHECK(i < g->count);
    return g->fixed[i];
}
static ThreeMMI32 size(void *p, ThreeMMU32 i)
{
    Group *g = (Group *)p;
    CHECK(i < g->count);
    return g->sizes[i];
}
static void *data(void *p, ThreeMMU32 i)
{
    Group *g = (Group *)p;
    CHECK(i < g->count && g->sizes[i] >= 0);
    return g->data[i];
}
static void *new_group(void *c, ThreeMMU32 width)
{
    Host *h = (Host *)c;
    CHECK(width == 20);
    event(h, 'n');
    ++h->allocations;
    if (h->new_fail)
        return NULL;
    return new_group_raw(h, 0);
}
static int insert(void *p, ThreeMMU32 i, ThreeMMU32 bytes, const void *variable, const void *meta)
{
    Group *g = (Group *)p;
    Host *h = g->host;
    event(h, 'i');
    if (h->fail_at >= 0 && h->inserts++ == h->fail_at)
        return 0;
    CHECK(i == g->count && i < 13 && bytes <= 64);
    memcpy(g->fixed[i], meta, 20);
    if (bytes)
        memcpy(g->data[i], variable, bytes);
    g->sizes[i] = (ThreeMMI32)bytes;
    ++g->count;
    return 1;
}
static void release_group(void *p)
{
    Group *g = (Group *)p;
    Host *h = g->host;
    ThreeMMI32 type;
    if (h->inspect_native && g->wire && g->count)
    {
        memcpy(&type, g->fixed[1], 4);
        CHECK(type == 1);
    }
    event(h, g->wire ? 'w' : 'r');
    --h->live;
    free(g);
}
static ThreeMMActorEventServices services(Host *h)
{
    ThreeMMActorEventServices s = {NULL, fixed_width, group_count, fixed, size, data, new_group, insert, release_group};
    s.context = h;
    return s;
}
static void initialize(Host *h)
{
    memset(h, 0, sizeof(*h));
    h->fail_at = -1;
}
static Group *runtime(Host *h)
{
    Group *g = new_group_raw(h, 0);
    ThreeMMU32 i, j;
    ThreeMMActorCostume c;
    ThreeMMActorSound s;
    ThreeMMI32 widths[] = {20, 8, sizeof(ThreeMMActorCostume), 48, 12, 4, sizeof(ThreeMMActorSound), 12, 4, 12, 4,
                           0,  48};
    g->count = 13;
    for (i = 0; i < 13; ++i)
    {
        g->sizes[i] = widths[i];
        for (j = 0; j < 20; ++j)
            g->fixed[i][j] = (unsigned char)(i + j);
        memcpy(g->fixed[i], &i, 4);
        memset(g->data[i], 0x40 + i, 64);
    }
    memset(&c, 0, sizeof(c));
    c.body_set = -99;
    c.custom = 2;
    c.tag.sid = -12;
    c.tag.cache = (void *)1;
    c.tag.type = 0x434f5354UL;
    c.tag.number = 77;
    memcpy(g->data[2], &c, sizeof(c));
    memset(&s, 0, sizeof(s));
    s.loop = 2;
    s.queue = -1;
    s.volume = -5;
    s.child = 33;
    s.tag.sid = 0;
    s.tag.cache = (void *)1;
    s.tag.type = 0x4d534e44UL;
    s.tag.number = 73;
    memcpy(g->data[6], &s, sizeof(s));
    return g;
}
static void foreign(Group *g)
{
    ThreeMMU32 i;
    unsigned j;
    for (i = 0; i < 13; ++i)
    {
        words(g->fixed[i], 5);
        if (i == 0)
        {
            words(g->data[i], 3);
            for (j = 0; j < 3; ++j)
                sw16(g->data[i] + 12 + j * 2);
        }
        else
            words(g->data[i], (unsigned)g->sizes[i] / 4);
    }
}
static void events(void)
{
    Host h;
    ThreeMMActorEventServices s;
    Group *source, *wire, *decoded, *again;
    ThreeMMActorCostume c;
    ThreeMMActorSound sound;
    unsigned char saved[13][64], padding[2];
    int other, fail;
    ThreeMMI32 old;
    initialize(&h);
    s = services(&h);
    source = runtime(&h);
    CHECK(three_mm_actor_valid_events(source, 0, THREE_MM_ACTOR_NATIVE, &s));
    for (fail = 0; fail < 13; ++fail)
    {
        old = source->sizes[fail];
        source->sizes[fail] = -1;
        CHECK(!three_mm_actor_valid_events(source, 0, 0, &s));
        source->sizes[fail] = old;
        h.fail_at = fail;
        h.inserts = 0;
        h.used = 0;
        CHECK(!three_mm_actor_write_events(source, &s) && h.live == 1 && source->count == 13 &&
              h.log[h.used - 1] == 'r');
    }
    h.fail_at = -1;
    h.new_fail = 1;
    CHECK(!three_mm_actor_write_events(source, &s) && h.live == 1);
    h.new_fail = 0;
    for (other = 0; other < 2; ++other)
    {
        h.used = 0;
        wire = (Group *)three_mm_actor_write_events(source, &s);
        CHECK(wire && h.live == 2 && wire->sizes[2] == 28 && wire->sizes[6] == 44);
        wire->wire = 1;
        memcpy(saved, wire->data, sizeof(saved));
        memcpy(padding, wire->data[0] + 18, 2);
        CHECK(!memcmp(wire->data[2] + 16, "\0\0\0\0", 4) && !memcmp(wire->data[6] + 32, "\0\0\0\0", 4));
        if (other)
            foreign(wire);
        h.inspect_native = 1;
        decoded = (Group *)three_mm_actor_read_events(wire, other ? THREE_MM_ACTOR_OTHER : THREE_MM_ACTOR_NATIVE, &s);
        CHECK(decoded && h.live == 2);
        CHECK(!memcmp(decoded->data[0] + 18, padding, 2));
        memcpy(&c, decoded->data[2], sizeof(c));
        memcpy(&sound, decoded->data[6], sizeof(sound));
        CHECK(c.custom == 2 && c.body_set == -99 && c.tag.sid == -12 && !c.tag.cache && sound.queue == -1 &&
              sound.loop == 2 && sound.volume == -5 && sound.child == 33 && !sound.tag.cache);
        again = (Group *)three_mm_actor_write_events(decoded, &s);
        CHECK(again && !memcmp(again->data, saved, sizeof(saved)));
        release_group(again);
        release_group(decoded);
    }
    for (fail = 0; fail < 13; ++fail)
    {
        h.fail_at = -1;
        wire = (Group *)three_mm_actor_write_events(source, &s);
        CHECK(wire != NULL);
        wire->wire = 1;
        foreign(wire);
        h.fail_at = fail;
        h.inserts = 0;
        h.used = 0;
        h.log[0] = 0;
        CHECK(!three_mm_actor_read_events(wire, THREE_MM_ACTOR_OTHER, &s) && h.live == 1);
        CHECK(h.used >= 2 && h.log[h.used - 2] == 'w' && h.log[h.used - 1] == 'r');
    }
    h.fail_at = -1;
    wire = (Group *)three_mm_actor_write_events(source, &s);
    wire->wire = 1;
    foreign(wire);
    h.new_fail = 1;
    h.used = 0;
    CHECK(!three_mm_actor_read_events(wire, THREE_MM_ACTOR_OTHER, &s) && h.live == 1 && !strcmp(h.log, "nw"));
    h.new_fail = 0;
    wire = (Group *)three_mm_actor_write_events(source, &s);
    wire->wire = 1;
    wire->width = 4;
    h.used = 0;
    h.inspect_native = 0;
    CHECK(!three_mm_actor_read_events(wire, 0, &s) && h.live == 1 && !strcmp(h.log, "w"));
    wire = (Group *)three_mm_actor_write_events(source, &s);
    wire->wire = 1;
    h.used = 0;
    CHECK(!three_mm_actor_read_events(wire, THREE_MM_ACTOR_INVALID_ORDER, &s) && !strcmp(h.log, "w"));
    CHECK(!three_mm_actor_read_events(NULL, 0, &s));
    release_group(source);
    CHECK(!h.live);
}
int main(void)
{
    headers();
    routes();
    events();
    puts("actor data passed");
    return 0;
}
