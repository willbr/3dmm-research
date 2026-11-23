#include "mm/actor_data.h"
#include "mm/actor_effect.h"
#include "chunk_writer_fixture.h"
#include "mm/actor_persistence.h"
#include "mm/collections.h"
#include "mm/movie_data.h"
#include "mm/movie_sound.h"
#include "mm/scene_data.h"
#include "mm/text_resource.h"
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
    MMU32 length;
    int reads, writes, fail_read, fail_write, fail_find;
} HeaderIO;
static int read_at(void *c, MMU32 offset, void *out, size_t bytes)
{
    HeaderIO *h = (HeaderIO *)c;
    ++h->reads;
    CHECK(offset + bytes <= 44);
    memcpy(out, h->bytes + offset, h->fail_read ? 3 : bytes);
    return !h->fail_read;
}
static int read_header_payload(void *c, const MMChunkEntry *entry, MMReadReceiver receive, void *out)
{
    HeaderIO *h = ((ChunkWriterFixture *)c)->context;
    MMReadStream stream;
    CHECK(entry->type == MM_ACTOR_CHUNK && entry->number == 7);
    stream.context = h;
    stream.length = h->length;
    stream.read_at = read_at;
    return receive(out, &stream);
}
static void header_access(void *c, MMChunkAccess *chunks)
{
    ChunkWriterFixture *fixture = c;
    *chunks = fixture_chunk_access(&fixture->index);
    if (((const HeaderIO *)fixture->context)->fail_find)
        chunks->count = 0;
}
static int header_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    CHECK(bytes == MM_ACTOR_HEADER_BYTES);
    extent->file = p;
    extent->offset = 64;
    extent->bytes = bytes;
    extent->flags = 0;
    return 1;
}
static int write_header(HeaderIO *h, const void *bytes, size_t size)
{
    CHECK(size == MM_ACTOR_HEADER_BYTES);
    ++h->writes;
    if (h->fail_write)
        return 0;
    memcpy(h->bytes, bytes, size);
    return 1;
}
static int write_header_extent(void *p, void *file, MMU32 offset, const void *bytes, size_t size)
{
    ChunkWriterFixture *archive = p;
    CHECK(file == p && offset == 64);
    return write_header(archive->context, bytes, size);
}
static void headers(void)
{
    HeaderIO h;
    ChunkWriterFixture fixture;
    MMArchiveWriter writer;
    MMMovieSessionGraph graph = {NULL, header_access, NULL, read_header_payload, NULL};
    MMActorHeader original, out, before;
    MMReadStream s;
    unsigned char legacy[40];
    int other;
    MMI32 id;
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
    writer = chunk_writer_fixture(&fixture, &h, header_extent, write_header_extent);
    graph.context = &fixture;
    fixture_chunk_add(&fixture.index, MM_ACTOR_CHUNK, 7)->entry.offset = 32;
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
        CHECK(mm_actor_read_header(&s, &out) && !memcmp(&out, &original, 44));
        memcpy(legacy, h.bytes, 24);
        memcpy(legacy + 24, h.bytes + 28, 16);
        memcpy(h.bytes, legacy, 40);
        s.length = 40;
        CHECK(mm_actor_read_header(&s, &out));
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
    CHECK(!mm_actor_read_header(&s, &out) && out.order == 3 && out.last_frame == original.source_id &&
          out.template_number == 99);
    memset(&out, 0xa5, 44);
    before = out;
    s.length = 20;
    h.reads = 0;
    CHECK(!mm_actor_read_header(&s, &out) && !h.reads && !memcmp(&out, &before, 44));
    s.length = 44;
    h.fail_read = 1;
    memcpy(h.bytes, &original, 44);
    CHECK(!mm_actor_read_header(&s, &out));
    CHECK(!memcmp(&out, &original, 3) && !memcmp((unsigned char *)&out + 3, (unsigned char *)&before + 3, 41));
    h.fail_read = 0;
    out = original;
    out.actor_id = 2147483647L;
    before = out;
    CHECK(!mm_actor_adjust_header_id(&out, 1) && !memcmp(&out, &before, 44));
    out.actor_id = (-2147483647L - 1);
    before = out;
    CHECK(!mm_actor_adjust_header_id(&out, -1) && !memcmp(&out, &before, 44));
    out.actor_id = 0;
    CHECK(mm_actor_adjust_header_id(&out, (-2147483647L - 1)) && out.actor_id == (-2147483647L - 1));
    out.actor_id = 2147483647L;
    CHECK(mm_actor_adjust_header_id(&out, (-2147483647L - 1)) && out.actor_id == -1);
    h.length = 44;
    memcpy(h.bytes, &original, 44);
    h.writes = 0;
    CHECK(mm_actor_adjust_id(7, 3, &graph, &writer) && h.writes == 1);
    memcpy(&id, h.bytes + 16, 4);
    CHECK(id == 20);
    out = original;
    out.actor_id = 2147483647L;
    memcpy(h.bytes, &out, 44);
    h.writes = 0;
    CHECK(!mm_actor_adjust_id(7, 1, &graph, &writer) && !h.writes);
    h.fail_find = 1;
    CHECK(!mm_actor_adjust_id(7, 0, &graph, &writer) && !h.writes);
    h.fail_find = 0;
    h.fail_write = 1;
    CHECK(!mm_actor_write_header(7, &original, &writer) && h.writes == 1);
    CHECK(fixture.index.rows[0].entry.offset == 64 && !memcmp(h.bytes, &out, sizeof(out)));
}
typedef struct Route
{
    MMU32 width, count, bits[8];
    int data_calls, released, count_calls;
} Route;
static MMU32 route_width(void *p)
{
    return ((Route *)p)->width;
}
static MMU32 route_count(void *p)
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
    MMU32 before[8];
    MMActorRouteServices s = {route_width, route_count, route_data, release_route};
    unsigned i;
    memset(&r, 0, sizeof(r));
    r.width = 16;
    CHECK(mm_actor_finish_route(NULL, 0, &s) == NULL);
    CHECK(mm_actor_finish_route(&r, MM_ACTOR_OTHER, &s) == &r && !r.data_calls);
    r.count = 2;
    for (i = 0; i < 8; ++i)
        r.bits[i] = 0x12345678UL + i;
    memcpy(before, r.bits, 32);
    r.count_calls = 0;
    CHECK(mm_actor_finish_route(&r, MM_ACTOR_NATIVE, &s) == &r && !r.data_calls && !r.count_calls);
    CHECK(mm_actor_finish_route(&r, MM_ACTOR_OTHER, &s) == &r && r.data_calls == 1 &&
          memcmp(before, r.bits, 32));
    CHECK(mm_actor_finish_route(&r, MM_ACTOR_OTHER, &s) == &r && !memcmp(before, r.bits, 32));
    r.width = 12;
    CHECK(!mm_actor_finish_route(&r, 0, &s) && r.released == 1);
    r.width = 16;
    CHECK(!mm_actor_finish_route(&r, MM_ACTOR_INVALID_ORDER, &s) && r.released == 2);
}

static void *create_route(void *p, MMU32 count, void **rows)
{
    Route *r = (Route *)p;
    CHECK(count <= 2);
    r->count = count;
    *rows = count ? r->bits : NULL;
    return r;
}

static int fail_track_body(void *p, MMU32 offset, void *out, size_t bytes)
{
    if (offset)
        return 0;
    memcpy(out, p, bytes);
    return 1;
}

static void route_streams(void)
{
    unsigned char bytes[44];
    const unsigned short one = 1;
    const MMU32 points[8] = {0xffffffffUL, 1, 0x7ffffffdUL, 65536, 17, 21, 0x80000000UL, 0};
    Route route;
    MMActorRouteReader reader = {NULL, create_route, release_route};
    MMCollectionHeader header;
    MMMemoryStream stream;
    void *out;
    int other;
    memset(&route, 0, sizeof(route));
    memset(&header, 0, sizeof(header));
    reader.context = &route;
    header.kind = MM_COLLECTION_ARRAY;
    header.width = MM_ACTOR_ROUTE_BYTES;
    header.count = 2;
    for (other = 0; other < 2; ++other)
    {
        header.big_endian = (*(const unsigned char *)&one == 0) != other;
        CHECK(mm_collection_header_write(&header, bytes, sizeof(bytes)) == MM_STORAGE_OK);
        memcpy(bytes + 12, points, sizeof(points));
        if (other)
            words(bytes + 12, 8);
        CHECK(mm_memory_stream_init(&stream, bytes, sizeof(bytes)));
        CHECK(mm_actor_read_route_stream(&stream.stream, &reader, &out) == MM_MOVIE_OK && out == &route &&
              route.count == 2 && !memcmp(route.bits, points, sizeof(points)));
    }
    out = NULL;
    stream.stream.context = bytes;
    stream.stream.read_at = fail_track_body;
    CHECK(mm_actor_read_route_stream(&stream.stream, &reader, &out) == MM_MOVIE_IO && !out && route.released == 1);
    header.count = 0;
    CHECK(mm_collection_header_write(&header, bytes, sizeof(bytes)) == MM_STORAGE_OK);
    CHECK(mm_memory_stream_init(&stream, bytes, 12));
    CHECK(mm_actor_read_route_stream(&stream.stream, &reader, &out) == MM_MOVIE_OK && out == &route && !route.count);
}
typedef struct Host Host;
typedef struct Group
{
    Host *host;
    int wire;
    MMU32 width, count;
    MMI32 sizes[13];
    unsigned char fixed[13][20], data[13][64];
} Group;
struct Host
{
    int live, new_fail, fail_at, insert_failure, inserts, allocations;
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
static MMU32 fixed_width(void *g)
{
    return ((Group *)g)->width;
}
static MMU32 group_count(void *g)
{
    return ((Group *)g)->count;
}
static void *fixed(void *p, MMU32 i)
{
    Group *g = (Group *)p;
    CHECK(i < g->count);
    return g->fixed[i];
}
static MMI32 size(void *p, MMU32 i)
{
    Group *g = (Group *)p;
    CHECK(i < g->count);
    return g->sizes[i];
}
static void *data(void *p, MMU32 i)
{
    Group *g = (Group *)p;
    CHECK(i < g->count && g->sizes[i] >= 0);
    return g->data[i];
}
static void *new_group(void *c, MMU32 width)
{
    Host *h = (Host *)c;
    CHECK(width == 20);
    event(h, 'n');
    ++h->allocations;
    if (h->new_fail)
        return NULL;
    return new_group_raw(h, 0);
}
static int insert(void *p, MMU32 i, MMU32 bytes, const void *variable, const void *meta)
{
    Group *g = (Group *)p;
    Host *h = g->host;
    event(h, 'i');
    if (h->fail_at >= 0 && h->inserts++ == h->fail_at)
        return h->insert_failure;
    CHECK(i == g->count && i < 13 && bytes <= 64);
    memcpy(g->fixed[i], meta, 20);
    if (bytes)
        memcpy(g->data[i], variable, bytes);
    g->sizes[i] = (MMI32)bytes;
    ++g->count;
    return 1;
}
static void release_group(void *p)
{
    Group *g = (Group *)p;
    Host *h = g->host;
    MMI32 type;
    if (h->inspect_native && g->wire && g->count)
    {
        memcpy(&type, g->fixed[1], 4);
        CHECK(type == 1);
    }
    event(h, g->wire ? 'w' : 'r');
    --h->live;
    free(g);
}
static MMActorEventServices services(Host *h)
{
    MMActorEventServices s = {NULL, fixed_width, group_count, fixed, size, data, new_group, insert, release_group};
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
    MMU32 i, j;
    MMActorCostume c;
    MMActorSound s;
    MMI32 widths[] = {20, 8, sizeof(MMActorCostume), 48, 12, 4, sizeof(MMActorSound), 12, 4, 12, 4,
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
    MMU32 i;
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
    MMActorEventServices s;
    Group *source, *wire, *decoded, *again;
    MMActorCostume c;
    MMActorSound sound;
    unsigned char saved[13][64], padding[2];
    int other, fail;
    MMI32 old;
    initialize(&h);
    s = services(&h);
    source = runtime(&h);
    CHECK(mm_actor_valid_events(source, 0, MM_ACTOR_NATIVE, &s));
    for (fail = 0; fail < 13; ++fail)
    {
        old = source->sizes[fail];
        source->sizes[fail] = -1;
        CHECK(!mm_actor_valid_events(source, 0, 0, &s));
        source->sizes[fail] = old;
        h.fail_at = fail;
        h.inserts = 0;
        h.used = 0;
        CHECK(!mm_actor_write_events(source, &s) && h.live == 1 && source->count == 13 &&
              h.log[h.used - 1] == 'r');
    }
    h.fail_at = -1;
    h.new_fail = 1;
    CHECK(!mm_actor_write_events(source, &s) && h.live == 1);
    h.new_fail = 0;
    for (other = 0; other < 2; ++other)
    {
        h.used = 0;
        wire = (Group *)mm_actor_write_events(source, &s);
        CHECK(wire && h.live == 2 && wire->sizes[2] == 28 && wire->sizes[6] == 44);
        wire->wire = 1;
        memcpy(saved, wire->data, sizeof(saved));
        memcpy(padding, wire->data[0] + 18, 2);
        CHECK(!memcmp(wire->data[2] + 16, "\0\0\0\0", 4) && !memcmp(wire->data[6] + 32, "\0\0\0\0", 4));
        if (other)
            foreign(wire);
        h.inspect_native = 1;
        decoded = (Group *)mm_actor_read_events(wire, other ? MM_ACTOR_OTHER : MM_ACTOR_NATIVE, &s);
        CHECK(decoded && h.live == 2);
        CHECK(!memcmp(decoded->data[0] + 18, padding, 2));
        memcpy(&c, decoded->data[2], sizeof(c));
        memcpy(&sound, decoded->data[6], sizeof(sound));
        CHECK(c.custom == 2 && c.body_set == -99 && c.tag.sid == -12 && !c.tag.cache && sound.queue == -1 &&
              sound.loop == 2 && sound.volume == -5 && sound.child == 33 && !sound.tag.cache);
        again = (Group *)mm_actor_write_events(decoded, &s);
        CHECK(again && !memcmp(again->data, saved, sizeof(saved)));
        release_group(again);
        release_group(decoded);
    }
    for (fail = 0; fail < 13; ++fail)
    {
        h.fail_at = -1;
        wire = (Group *)mm_actor_write_events(source, &s);
        CHECK(wire != NULL);
        wire->wire = 1;
        foreign(wire);
        h.fail_at = fail;
        h.inserts = 0;
        h.used = 0;
        h.log[0] = 0;
        CHECK(!mm_actor_read_events(wire, MM_ACTOR_OTHER, &s) && h.live == 1);
        CHECK(h.used >= 2 && h.log[h.used - 2] == 'w' && h.log[h.used - 1] == 'r');
    }
    h.fail_at = -1;
    wire = (Group *)mm_actor_write_events(source, &s);
    wire->wire = 1;
    foreign(wire);
    h.new_fail = 1;
    h.used = 0;
    CHECK(!mm_actor_read_events(wire, MM_ACTOR_OTHER, &s) && h.live == 1 && !strcmp(h.log, "nw"));
    h.new_fail = 0;
    wire = (Group *)mm_actor_write_events(source, &s);
    wire->wire = 1;
    wire->width = 4;
    h.used = 0;
    h.inspect_native = 0;
    CHECK(!mm_actor_read_events(wire, 0, &s) && h.live == 1 && !strcmp(h.log, "w"));
    wire = (Group *)mm_actor_write_events(source, &s);
    wire->wire = 1;
    h.used = 0;
    CHECK(!mm_actor_read_events(wire, MM_ACTOR_INVALID_ORDER, &s) && !strcmp(h.log, "w"));
    CHECK(!mm_actor_read_events(NULL, 0, &s));
    release_group(source);
    CHECK(!h.live);
}

static void event_streams(void)
{
    Host h;
    MMActorEventServices s;
    Group *source, *wire, *actual;
    MMActorCostume costume;
    MMActorSound sound;
    MMCollectionHeader header;
    MMMemoryStream stream;
    unsigned char bytes[1024], *locations;
    const unsigned short one = 1;
    MMU32 row, length, offset, size;
    void *out = NULL;
    int other;
    initialize(&h);
    s = services(&h);
    source = runtime(&h);
    wire = (Group *)mm_actor_write_events(source, &s);
    CHECK(wire != NULL);
    memset(&header, 0, sizeof(header));
    header.kind = MM_COLLECTION_GROUP;
    header.width = wire->width;
    header.count = wire->count;
    header.free_count = -1;
    for (row = 0; row < wire->count; ++row)
        header.bytes += header.width + (MMU32)wire->sizes[row];
    locations = bytes + 20 + header.bytes;
    length = 20 + header.bytes + header.count * 8;
    CHECK(length <= sizeof(bytes));
    for (other = 0; other < 2; ++other)
    {
        header.big_endian = (*(const unsigned char *)&one == 0) != other;
        CHECK(mm_collection_header_write(&header, bytes, length) == MM_STORAGE_OK);
        if (other)
            foreign(wire);
        offset = 0;
        for (row = 0; row < wire->count; ++row)
        {
            size = header.width + (MMU32)wire->sizes[row];
            memcpy(bytes + 20 + offset, wire->fixed[row], header.width);
            memcpy(bytes + 20 + offset + header.width, wire->data[row], (size_t)wire->sizes[row]);
            memcpy(locations + row * 8, &offset, 4);
            memcpy(locations + row * 8 + 4, &size, 4);
            if (other)
                words(locations + row * 8, 2);
            offset += size;
        }
        CHECK(mm_memory_stream_init(&stream, bytes, length));
        CHECK(mm_actor_read_events_stream(&stream.stream, &s, &out) == MM_MOVIE_OK && h.live == 3);
        actual = (Group *)out;
        CHECK(actual->count == source->count && !memcmp(actual->fixed, source->fixed, sizeof(actual->fixed)));
        CHECK(!memcmp(actual->data[0], source->data[0], 20) && !memcmp(actual->data[12], source->data[12], 48));
        memcpy(&costume, actual->data[2], sizeof(costume));
        memcpy(&sound, actual->data[6], sizeof(sound));
        CHECK(!costume.tag.cache && costume.tag.number == 77 && !sound.tag.cache && sound.tag.number == 73);
        release_group(actual);
    }
    h.fail_at = 4;
    out = NULL;
    CHECK(mm_actor_read_events_stream(&stream.stream, &s, &out) == MM_MOVIE_MEMORY && !out && h.live == 2);
    h.inserts = 0;
    h.insert_failure = -1;
    out = source;
    CHECK(mm_actor_read_events_stream(&stream.stream, &s, &out) == MM_MOVIE_BAD_FILE && out == source && h.live == 2);
    memset(locations + (header.count - 1) * 8, 0xff, 4);
    h.used = 0;
    out = NULL;
    CHECK(mm_actor_read_events_stream(&stream.stream, &s, &out) == MM_MOVIE_BAD_FILE && !out && h.used == 0);
    release_group(wire);
    release_group(source);
    CHECK(!h.live);
}

typedef struct ActorStore
{
    HeaderIO header;
    ChunkWriterFixture archive;
    unsigned char path[28], events[208];
    unsigned char tmpl[MM_TEMPLATE_HEADER_BYTES], descriptor[MM_TEXT_DESCRIPTOR_BYTES];
    MMU32 event_bytes;
    int copies, linked, tracks, discarded, reject, reuse, reject_track, fail_track_write;
    int text_actor, reject_template, template_writes;
} ActorStore;

enum { header_offset = 64, path_offset = 128, events_offset = 256, template_offset = 768, descriptor_offset = 1024 };
static int store_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    ActorStore *store = ((ChunkIndexFixture *)p)->observer;
    extent->file = &store->archive;
    extent->offset = header_offset;
    extent->flags = 0;
    extent->bytes = bytes;
    if (bytes == MM_ACTOR_HEADER_BYTES)
        return 1;
    if (store->text_actor && (bytes == sizeof(store->tmpl) || bytes == sizeof(store->descriptor)))
    {
        extent->offset = bytes == sizeof(store->tmpl) ? template_offset : descriptor_offset;
        return 1;
    }
    if (++store->tracks == 1)
    {
        CHECK(bytes == sizeof(store->path));
        extent->offset = path_offset;
    }
    else
    {
        CHECK(store->tracks == 2 && (store->linked || store->text_actor) && bytes <= sizeof(store->events));
        extent->offset = events_offset;
        store->event_bytes = bytes;
    }
    return 1;
}

static int store_write(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    ChunkWriterFixture *fixture = p;
    ActorStore *store = fixture->context;
    CHECK(file == p);
    if (offset == header_offset)
        return write_header(&store->header, data, bytes);
    if (offset == template_offset || offset == descriptor_offset)
    {
        CHECK(bytes == (offset == template_offset ? sizeof(store->tmpl) : sizeof(store->descriptor)));
        memcpy(offset == template_offset ? store->tmpl : store->descriptor, data, bytes);
        ++store->template_writes;
        return 1;
    }
    CHECK(fixture->file_refs == 1 && store->tracks >= 1 && store->tracks <= 2);
    if (store->fail_track_write == store->tracks)
        return 0;
    if (store->tracks == 1)
    {
        CHECK(offset >= path_offset && offset - path_offset + bytes <= sizeof(store->path));
        memcpy(store->path + offset - path_offset, data, bytes);
    }
    else
    {
        CHECK(offset >= events_offset && offset - events_offset + bytes <= store->event_bytes);
        memcpy(store->events + offset - events_offset, data, bytes);
    }
    return 1;
}

static int store_copy(void *p, const MMTag *tag, MMU32 *number, int *created)
{
    ActorStore *store = (ActorStore *)p;
    CHECK(tag->sid == 0 && tag->cache == p && tag->type == 0x4d54524cUL && tag->number == 77);
    ++store->copies;
    *number = 99;
    *created = !store->linked && !store->reuse;
    if (*created)
        fixture_chunk_add(&store->archive.index, tag->type, *number);
    return 1;
}

static int store_copy_sound(void *p, const MMTag *tag, MMU32 *number, int *created)
{
    ActorStore *store = p;
    CHECK(tag->sid == 0 && tag->cache == p && tag->type == MM_SOUND_CHUNK && tag->number == 11);
    *number = 88;
    *created = 1;
    fixture_chunk_add(&store->archive.index, tag->type, *number);
    return MM_CHUNK_OK;
}

static int store_insert(void *p, MMU32 parent, const MMChunkChild *child)
{
    ActorStore *store = (ActorStore *)p;
    if (child->type == MM_SOUND_CHUNK)
    {
        CHECK(parent == 8 && child->slot == 3 && child->number == 88);
        return 1;
    }
    if (child->type == MM_TDT)
    {
        CHECK(store->text_actor && parent == 0 && child->slot == 0);
        return 1;
    }
    CHECK(parent == 7 && child->slot == 0);
    if (child->type == MM_TMPL)
    {
        CHECK(store->text_actor && child->number == 19);
        return !store->reject_template;
    }
    if (child->type == MM_ACTOR_ROUTE_CHUNK || child->type == MM_ACTOR_EVENTS_CHUNK)
    {
        CHECK(child->number == 0 && store->tracks == (child->type == MM_ACTOR_ROUTE_CHUNK ? 1 : 2));
        return store->tracks != store->reject_track;
    }
    CHECK(child->type == 0x4d54524cUL && child->number == 99 && !store->linked);
    if (store->reject)
        return 0;
    store->linked = 1;
    return 1;
}

static void store_erase(void *p, const MMChunkEntry *entry)
{
    ActorStore *store = (ActorStore *)p;
    if (entry->type == 0x4d54524cUL)
    {
        CHECK(entry->number == 99 && !entry->parent_count && !store->linked);
        ++store->discarded;
    }
}

static void store_actor(ActorStore *store)
{
    MMActorStoreServices s = {0};
    MMArchiveWriter archive;
    MMActorOwner owner;
    MMActorEventFactory factory = {NULL, 0};
    MMMemoryStream saved;
    MMActorEventServices services = mm_actor_events_services(&factory);
    MMActorCostume costume = {0};
    MMActorCostume decoded;
    MMActorSound sound = {0}, decoded_sound;
    MMActorEvent event = {0};
    MMActorRoutePoint point = {0};
    const MMU32 failed_track_children[] = {0, 2, 1, 3};
    void *out;
    int mode, reject, reuse;
    s.context = store;
    s.archive = &archive;
    s.assets.context = store;
    s.assets.copy_reference = store_copy;
    s.assets.copy_sound = store_copy_sound;
    mm_actor_owner_init(&owner);
    owner.runtime.template_tag.sid = 2;
    owner.runtime.origin.dxr = -65537;
    owner.runtime.last = 13;
    owner.runtime.actor = 47;
    costume.tag.cache = store;
    costume.tag.type = 0x4d54524cUL;
    costume.tag.number = 77;
    owner.track.route = mm_actor_path_create(NULL, 0, 0);
    owner.track.events = mm_actor_events_create(&factory, 0, 0);
    CHECK(owner.track.route && owner.track.events && mm_actor_path_insert(owner.track.route, 0, &point));
    event.aet = MM_ACTOR_COSTUME;
    CHECK(mm_actor_events_insert(owner.track.events, 0, sizeof(costume), &costume, &event));
    CHECK(mm_actor_events_insert(owner.track.events, 1, sizeof(costume), &costume, &event));
    event.aet = MM_ACTOR_SOUND;
    sound.child = 3;
    sound.tag.cache = store;
    sound.tag.type = MM_SOUND_CHUNK;
    sound.tag.number = 11;
    CHECK(mm_actor_events_insert(owner.track.events, 2, sizeof(sound), &sound, &event));
    for (mode = 7; mode >= 0; --mode)
    {
        MMChunkAccess index;
        MMChunkEntry entry;
        MMU32 row;
        memset(store, 0, sizeof(*store));
        store->header.length = sizeof(store->header.bytes);
        store->reject = reject = mode < 4 && (mode & 1);
        store->reuse = reuse = mode < 4 && (mode & 2);
        if (mode >= 6)
            store->fail_track_write = mode - 5;
        else if (mode >= 4)
            store->reject_track = mode - 3;
        archive = chunk_writer_fixture(&store->archive, store, store_extent, store_write);
        fixture_chunk_add(&store->archive.index, MM_ACTOR_CHUNK, 7);
        fixture_chunk_add(&store->archive.index, MM_SCENE_CHUNK, 8);
        if (reuse)
            fixture_chunk_add(&store->archive.index, costume.tag.type, 99);
        store->archive.index.before_insert = store_insert;
        store->archive.index.before_delete = store_erase;
        s.assets.edit = archive.edit;
        CHECK(mm_actor_store(7, 8, &owner, &s) == (mode < 4 && !reject));
        CHECK(!store->archive.file_refs);
        CHECK(!memcmp(mm_actor_events_payload(owner.track.events, 0, NULL), &costume, sizeof(costume)) &&
              !memcmp(mm_actor_events_payload(owner.track.events, 1, NULL), &costume, sizeof(costume)));
        CHECK(!memcmp(mm_actor_events_payload(owner.track.events, 2, NULL), &sound, sizeof(sound)));
        index = fixture_chunk_access(&store->archive.index);
        CHECK(mm_chunk_find(&index, MM_ACTOR_CHUNK, 7, &row) == MM_CHUNK_OK);
        CHECK(mm_chunk_at(&index, row, &entry) == MM_CHUNK_OK);
        if (mode >= 4)
        {
            CHECK(entry.child_count == failed_track_children[mode - 4]);
            CHECK(store->tracks == 1 + (mode & 1));
            continue;
        }
        CHECK(store->header.writes == 1 && store->tracks == 2 - reject && store->copies == 2 - reject &&
              store->discarded == (reject && !reuse) && store->linked == !reject);
        CHECK(entry.child_count == (reject ? 1U : 3U));
        CHECK(mm_chunk_find(&index, costume.tag.type, 99, &row) ==
              (reject && !reuse ? MM_CHUNK_NOT_FOUND : MM_CHUNK_OK));
        if (!reject || reuse)
        {
            CHECK(mm_chunk_at(&index, row, &entry) == MM_CHUNK_OK && entry.parent_count == (MMU32)!reject);
            CHECK((entry.flags & MM_CHUNK_LONER) == (reuse ? MM_CHUNK_LONER : 0));
        }
    }
    CHECK(mm_memory_stream_init(&saved, store->events, store->event_bytes));
    CHECK(mm_actor_read_events_stream(&saved.stream, &services, &out) == MM_MOVIE_OK);
    CHECK(mm_actor_events_count((MMActorEventTable *)out) == 3);
    CHECK(mm_actor_events_get_payload((MMActorEventTable *)out, 0, &decoded));
    CHECK(decoded.tag.number == 99 && !decoded.tag.cache);
    CHECK(mm_actor_events_get_payload((MMActorEventTable *)out, 2, &decoded_sound));
    CHECK(decoded_sound.child == 3 && decoded_sound.tag.number == 88 && !decoded_sound.tag.cache);
    CHECK(!memcmp(mm_actor_events_payload((MMActorEventTable *)out, 0, NULL),
                  mm_actor_events_payload((MMActorEventTable *)out, 1, NULL), sizeof(decoded)));
    mm_actor_events_release((MMActorEventTable *)out);
    mm_actor_events_release(owner.track.events);
    mm_actor_path_release(owner.track.route);
}

static void store_text_actor(void)
{
    const MMTemplateOwnerHost host = {NULL, 1, NULL};
    const MMActorEventFactory factory = {NULL, 0};
    const MMActorRoutePoint point = {0};
    const MMActorTimelineServices timeline = mm_actor_track_timeline_services();
    MMTemplateOwner *text = mm_template_owner_create(&host, 1);
    MMActorStoreServices services = {0};
    MMActorOwner owner;
    ActorStore store;
    MMArchiveWriter archive;
    MMStringValue name;
    MMTextDescriptor *descriptor;
    int mode;
    CHECK(text != NULL);
    name = mm_template_owner_name(text);
    mm_string_value_set(&name, "Test text", 9);
    mm_template_owner_state(text)->flags = MM_TEMPLATE_TEXT;
    descriptor = mm_template_owner_descriptor(text);
    descriptor->shape = 5;
    descriptor->font.sid = 3;
    descriptor->font.type = 0x54444620UL;
    descriptor->font.number = 22;
    mm_actor_owner_init(&owner);
    owner.runtime.template_object = text;
    owner.runtime.template_tag.sid = 0;
    owner.runtime.template_tag.type = MM_TMPL;
    owner.runtime.template_tag.number = 19;
    owner.runtime.first = 6;
    owner.track.route = mm_actor_path_create(NULL, 0, 0);
    owner.track.events = mm_actor_events_create(&factory, 0, 0);
    CHECK(owner.track.route && owner.track.events && mm_actor_path_insert(owner.track.route, 0, &point));
    services.archive = &archive;
    services.timeline = &timeline;
    for (mode = 0; mode < 5; ++mode)
    {
        const int reuse = mode == 1 || mode == 2 || mode == 4;
        MMChunkChild link = {MM_TMPL, 19, 0};
        MMChunkAccess chunks;
        MMU32 number, row;
        memset(&store, 0, sizeof(store));
        store.text_actor = 1;
        store.header.length = sizeof(store.header.bytes);
        archive = chunk_writer_fixture(&store.archive, &store, store_extent, store_write);
        archive.system = MM_TEXT_WINDOWS;
        store.archive.index.before_insert = store_insert;
        fixture_chunk_add(&store.archive.index, MM_ACTOR_CHUNK, 7);
        if (reuse)
            fixture_chunk_add(&store.archive.index, link.type, link.number);
        if (mode == 2)
            CHECK(mm_chunk_adopt(&archive.edit, MM_ACTOR_CHUNK, 7, &link, 1) == MM_CHUNK_OK);
        store.reject_template = mode >= 3;
        owner.runtime.life_dirty = 1;
        CHECK(mm_actor_store(7, 8, &owner, &services) == (mode < 3));
        CHECK(owner.runtime.last == 6 && !owner.runtime.life_dirty && !store.archive.file_refs);
        CHECK(store.template_writes == (reuse ? 0 : 2));
        chunks = fixture_chunk_access(&store.archive.index);
        CHECK(mm_chunk_find(&chunks, MM_TMPL, 0, &row) == MM_CHUNK_NOT_FOUND);
        CHECK(mm_chunk_find(&chunks, MM_TMPL, 19, &row) == (mode == 3 ? MM_CHUNK_NOT_FOUND : MM_CHUNK_OK));
        CHECK(mm_chunk_link_number(&chunks, MM_ACTOR_CHUNK, 7, 0, MM_TMPL, &number) ==
              (mode < 3 ? MM_CHUNK_OK : MM_CHUNK_NOT_FOUND));
        CHECK(mm_chunk_find(&chunks, MM_TDT, 0, &row) == (mode == 0 ? MM_CHUNK_OK : MM_CHUNK_NOT_FOUND));
        if (!mode)
        {
            MMTextDescriptor written;
            CHECK(number == 19 && mm_chunk_link_number(&chunks, MM_TMPL, 19, 0, MM_TDT, &number) == MM_CHUNK_OK);
            CHECK(mm_text_decode_descriptor(store.descriptor, 0, &written) && written.shape == 5 &&
                  written.font.sid == 3 && written.font.number == 22);
            CHECK(!memcmp(store.archive.name, "\003\003\011Test text", 13));
        }
    }
    mm_actor_events_release(owner.track.events);
    mm_actor_path_release(owner.track.route);
    mm_template_owner_release(text);
}

typedef struct TrackArchive
{
    ChunkWriterFixture archive;
    unsigned char bytes[512];
    MMU32 lengths[2];
    int creates, tracks, saves, fail_create, fail_track, fail_save;
} TrackArchive;

static int track_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    TrackArchive *draft = ((ChunkIndexFixture *)p)->observer;
    CHECK(draft->tracks < 2 && bytes <= 256);
    extent->file = &draft->archive;
    extent->offset = (MMU32)draft->tracks * 256;
    extent->bytes = bytes;
    extent->flags = 0;
    draft->lengths[draft->tracks++] = bytes;
    return 1;
}

static int track_write(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    ChunkWriterFixture *fixture = p;
    TrackArchive *draft = fixture->context;
    int track = offset / 256;
    CHECK(file == p && fixture->file_refs == 1 && track < draft->tracks &&
          offset % 256 + bytes <= draft->lengths[track]);
    if (draft->fail_track == track + 1)
        return 0;
    memcpy(draft->bytes + offset, data, bytes);
    return 1;
}

static int create_track_archive(void *p, MMArchiveWriter *out)
{
    TrackArchive *draft = p;
    ++draft->creates;
    if (draft->fail_create)
        return 0;
    *out = chunk_writer_fixture(&draft->archive, draft, track_extent, track_write);
    return 1;
}

static int save_track_archive(void *p)
{
    TrackArchive *draft = p;
    CHECK(!draft->archive.file_refs);
    ++draft->saves;
    return !draft->fail_save;
}

static void store_edit_tracks(void)
{
    TrackArchive draft;
    MMActorTrackArchive archive = {NULL, create_track_archive, save_track_archive};
    MMActorTrack track;
    MMActorEventFactory factory = {NULL, 0};
    MMActorEventServices events = mm_actor_events_services(&factory);
    MMActorEvent event = {0};
    MMActorCostume costume = {0}, decoded;
    MMActorRoutePoint point = {0};
    MMMemoryStream stream;
    MMChunkAccess chunks;
    MMU32 number;
    void *out;
    int mode;
    archive.context = &draft;
    track.events = mm_actor_events_create(&factory, 0, 0);
    track.route = mm_actor_path_create(NULL, 0, 0);
    CHECK(track.events && track.route && mm_actor_path_insert(track.route, 0, &point));
    event.aet = MM_ACTOR_COSTUME;
    costume.tag.number = 77;
    costume.tag.cache = &draft;
    CHECK(mm_actor_events_insert(track.events, 0, sizeof(costume), &costume, &event));
    for (mode = 0; mode <= 6; ++mode)
    {
        memset(&draft, 0, sizeof(draft));
        draft.fail_create = mode == 2;
        draft.fail_track = mode == 3 ? 1 : mode == 4 ? 2 : 0;
        draft.fail_save = mode == 5;
        if (mode == 6)
        {
            event.aet = -1;
            CHECK(mm_actor_events_set_fixed(track.events, 0, &event));
        }
        CHECK(mm_actor_store_edit_tracks(&track, mode != 1, &archive) == (mode < 2));
        CHECK(draft.creates == (mode != 6) && draft.saves == (mode < 2 || mode == 5) && !draft.archive.file_refs);
        if (mode >= 2)
            continue;
        chunks = fixture_chunk_access(&draft.archive.index);
        CHECK(chunks.count == (mode ? 1U : 2U) && mm_chunk_find(&chunks, MM_ACTOR_EVENTS_CHUNK, 0, &number) == MM_CHUNK_OK);
        CHECK(mm_memory_stream_init(&stream, draft.bytes, draft.lengths[0]));
        CHECK(mm_actor_read_events_stream(&stream.stream, &events, &out) == MM_MOVIE_OK);
        CHECK(mm_actor_events_get_payload(out, 0, &decoded) && decoded.tag.number == 77 && !decoded.tag.cache);
        mm_actor_events_release(out);
        if (mode == 0)
            CHECK(draft.lengths[1] == mm_actor_path_wire_size(track.route) &&
                  mm_chunk_find(&chunks, MM_ACTOR_ROUTE_CHUNK, 0, &number) == MM_CHUNK_OK);
    }
    mm_actor_events_release(track.events);
    mm_actor_path_release(track.route);
}

typedef struct ActorLoad
{
    ActorStore *saved;
    MMActorOwner owner;
    int legacy, missing_link, fail, stage, refs, released, diagnostics;
} ActorLoad;

static int load_entry(void *p, MMU32 row, MMChunkEntry *out)
{
    static const MMU32 types[] = {MM_ACTOR_CHUNK, MM_ACTOR_EVENTS_CHUNK, MM_ACTOR_ROUTE_CHUNK};
    ActorLoad *load = (ActorLoad *)p;
    memset(out, 0, sizeof(*out));
    out->type = types[row];
    out->number = row ? 0 : 7;
    out->child_count = row ? 0 : 3 - load->missing_link;
    return 1;
}

static int load_child(void *p, MMU32 row, MMU32 child, MMChunkChild *out)
{
    static const MMU32 types[] = {MM_ACTOR_EVENTS_CHUNK, MM_ACTOR_ROUTE_CHUNK, MM_TMPL};
    (void)p;
    CHECK(row == 0);
    out->type = types[child];
    out->number = child == 2 ? 99 : 0;
    out->slot = 0;
    return 1;
}

static void load_access(void *p, MMChunkAccess *out)
{
    MMChunkAccess access = {NULL, 3, load_entry, load_child};
    *out = access;
    out->context = p;
}

static int load_payload(void *p, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    ActorLoad *load = (ActorLoad *)p;
    MMMemoryStream stream;
    MMActorHeader header;
    unsigned char old[MM_ACTOR_OLD_HEADER_BYTES];
    if (entry->type == MM_ACTOR_CHUNK)
    {
        memcpy(&header, load->saved->header.bytes, sizeof(header));
        header.source_id = 0;
        header.template_type = MM_TMPL;
        header.template_number = 17;
        header.first_frame = 4;
        header.wire_pointer_pad = 0xbad01234UL;
        if (load->legacy)
        {
            memcpy(old, &header, 24);
            memcpy(old + 24, &header.source_id, 16);
            CHECK(mm_memory_stream_init(&stream, old, sizeof(old)));
        }
        else
            CHECK(mm_memory_stream_init(&stream, &header, sizeof(header)));
    }
    else if (entry->type == MM_ACTOR_ROUTE_CHUNK)
        CHECK(mm_memory_stream_init(&stream, load->saved->path, sizeof(load->saved->path)));
    else
    {
        if (load->fail == 1)
            return MM_MOVIE_IO;
        CHECK(mm_memory_stream_init(&stream, load->saved->events, load->saved->event_bytes));
    }
    return receive(out, &stream.stream);
}

static void retain_cache(void *p) { ++((ActorLoad *)p)->refs; }
static void release_cache(void *p) { CHECK(--((ActorLoad *)p)->refs >= 0); }
static const MMTagServices load_tags = {
    NULL, retain_cache, release_cache, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void *load_sound_storage(void *p, void *data, MMU32 previous, MMU32 bytes, int priority)
{
    ActorLoad *load = (ActorLoad *)p;
    MMTag tag;
    (void)previous;
    (void)priority;
    CHECK(load->stage <= 1 && load->refs == 4);
    CHECK(mm_actor_track_get_tag(load->owner.track.events, 0, &tag) && tag.cache == p && tag.number == 99);
    CHECK(mm_actor_track_get_tag(load->owner.track.events, 2, &tag) && tag.cache == p && tag.number == 88);
    load->stage = 1;
    if (load->fail == 2)
        return NULL;
    return realloc(data, bytes);
}

static void release_sound_storage(void *p, void *data, MMU32 bytes)
{
    ActorLoad *load = (ActorLoad *)p;
    (void)bytes;
    CHECK(load->released && !load->refs);
    free(data);
}

static void *create_loaded_actor(void *p, MMActorOwner **out)
{
    ActorLoad *load = (ActorLoad *)p;
    MMStorageAllocator allocator = {NULL, load_sound_storage, release_sound_storage};
    allocator.context = load;
    mm_actor_owner_init(&load->owner);
    CHECK(mm_actor_sounds_init(&load->owner.sounds, &allocator));
    *out = &load->owner;
    return load;
}

static void release_loaded_actor(void *p, void *actor)
{
    ActorLoad *load = (ActorLoad *)p;
    CHECK(actor == load && !load->released++);
    CHECK(load->fail != 1 || (load->owner.track.route && !load->owner.track.events && !load->refs));
    mm_tag_close(&load->owner.runtime.template_tag, &load_tags);
    mm_actor_track_dispose(&load->owner.track, &load_tags);
    CHECK(!load->refs);
    mm_actor_sounds_dispose(&load->owner.sounds);
}

static void *load_template(void *p, const MMTag *tag)
{
    ActorLoad *load = (ActorLoad *)p;
    CHECK(load->stage++ == 1 && tag->cache == p && tag->number == (load->missing_link ? 17U : 99U));
    return load->fail == 3 ? NULL : p;
}

static MMActorTimelineServices load_timeline(void *p, void *actor)
{
    ActorLoad *load = (ActorLoad *)p;
    CHECK(actor == load && load->stage++ == 2 && load->legacy);
    CHECK(load->owner.runtime.life_dirty && load->owner.runtime.template_object == p);
    return mm_actor_track_timeline_services();
}

static void load_diagnostic(void *p, int diagnostic)
{
    ActorLoad *load = (ActorLoad *)p;
    CHECK(!load->released);
    CHECK(diagnostic == MM_ACTOR_LOAD_FAILED || diagnostic == MM_ACTOR_LOAD_TEMPLATE_LINK);
    load->diagnostics |= 1 << diagnostic;
}

static void load_actor(ActorStore *saved)
{
    ActorLoad load;
    MMMovieSessionGraph graph = {NULL, load_access, NULL, load_payload, NULL};
    MMActorLoadServices s = {NULL, &load_tags, NULL, 0, create_loaded_actor, release_loaded_actor,
                            load_template, load_timeline, load_diagnostic};
    int run;
    graph.context = s.context = &load;
    for (run = 0; run < 6; ++run)
    {
        memset(&load, 0, sizeof(load));
        load.saved = saved;
        load.fail = run <= 3 ? run : 0;
        load.legacy = run == 4;
        load.missing_link = run == 5;
        CHECK(mm_actor_load(&graph, 7, &load, &s) == (load.fail ? NULL : &load));
        CHECK(load.owner.runtime.actor == 47 && load.owner.runtime.origin.dxr == -65537 && load.owner.runtime.first == 4);
        if (load.fail)
            CHECK(load.released && load.stage == load.fail - 1 && load.diagnostics == (1 << MM_ACTOR_LOAD_FAILED));
        else
        {
            CHECK(load.refs == 4 && mm_actor_path_count(load.owner.track.route) == 1);
            CHECK(load.stage == 2 + load.legacy && load.owner.runtime.last == (load.legacy ? 4 : 13));
            CHECK(!load.owner.runtime.life_dirty);
            CHECK(load.diagnostics == (load.missing_link << MM_ACTOR_LOAD_TEMPLATE_LINK));
            release_loaded_actor(&load, &load);
        }
    }
}

typedef struct RangeCheck
{
    MMActorOwner *owner;
    int resolves, diagnostics;
} RangeCheck;

static void range_diagnostic(void *p, int issue)
{
    RangeCheck *check = p;
    check->diagnostics |= 1 << issue;
}

static int range_unavailable(void *p, MMI32 action, MMI32 cel, MMI32 *distance)
{
    RangeCheck *test = p;
    CHECK(action == 0 && cel == 0 && *distance == 0);
    CHECK(test->owner->runtime.last == 2);
    ++test->resolves;
    return 0;
}

static void actor_range(void)
{
    const MMActorEventFactory factory = {NULL, 0};
    const MMActorEntrance entrance = {0};
    const MMI32 action_step = -65536L;
    MMActorOwner owner;
    MMActorTimelineServices timeline = mm_actor_track_timeline_services();
    MMActorEvent event = {0};
    MMActorRoutePoint point = {0};
    RangeCheck test;
    MMI32 first = -99, last = -99;
    mm_actor_owner_init(&owner);
    test.owner = &owner;
    test.resolves = test.diagnostics = 0;
    timeline.context = &test;
    timeline.resolve = range_unavailable;
    timeline.diagnostic = range_diagnostic;
    CHECK(mm_actor_owner_frame_range(&owner, &first, &last, &timeline) == MM_ACTOR_RANGE_UNPLACED);
    CHECK(first == -99 && last == -99);
    owner.runtime.first = 1;
    owner.runtime.life_dirty = 1;
    CHECK(mm_actor_owner_frame_range(&owner, &first, NULL, &timeline) == MM_ACTOR_RANGE_READY && first == 1);
    CHECK(owner.runtime.life_dirty && !test.resolves && !test.diagnostics);
    owner.track.route = mm_actor_path_create(NULL, 0, 0);
    owner.track.events = mm_actor_events_create(&factory, 0, 0);
    point.dwr = 65536L;
    CHECK(owner.track.route && owner.track.events && mm_actor_path_insert(owner.track.route, 0, &point));
    point.dwr = 0;
    CHECK(mm_actor_path_insert(owner.track.route, 1, &point));
    CHECK(mm_actor_path_insert(owner.track.route, 2, &point));
    event.aet = MM_ACTOR_ADD;
    event.nfrm = 1;
    CHECK(mm_actor_events_insert(owner.track.events, 0, sizeof(entrance), &entrance, &event));
    event.aet = MM_ACTOR_STEP;
    CHECK(mm_actor_events_insert(owner.track.events, 1, sizeof(action_step), &action_step, &event));
    event.aet = MM_ACTOR_ADD;
    event.nfrm = 8;
    event.rtel.irpt = 2;
    CHECK(mm_actor_events_insert(owner.track.events, 2, sizeof(entrance), &entrance, &event));
    CHECK(mm_actor_owner_frame_range(&owner, &first, &last, &timeline) == MM_ACTOR_RANGE_PARTIAL);
    CHECK(first == 1 && last == -99 && owner.runtime.last == 2 && owner.runtime.life_dirty && test.resolves == 1);
    CHECK(test.diagnostics == (1 << MM_ACTOR_LIFETIME_INCOMPLETE));
    owner.runtime.recording.recording = 1;
    CHECK(mm_actor_owner_frame_range(&owner, NULL, &last, &timeline) == MM_ACTOR_RANGE_READY);
    CHECK(last == 2 && owner.runtime.life_dirty && test.resolves == 1);
    owner.runtime.life_dirty = 0;
    owner.runtime.last = 0;
    CHECK(mm_actor_owner_frame_range(&owner, &first, &last, &timeline) == MM_ACTOR_RANGE_READY);
    CHECK(last == 0 && (test.diagnostics & (1 << MM_ACTOR_LIFETIME_BAD_RANGE)));
    mm_actor_events_release(owner.track.events);
    mm_actor_path_release(owner.track.route);
}

int main(void)
{
    ActorStore store;
    headers();
    routes();
    route_streams();
    events();
    event_streams();
    store_actor(&store);
    load_actor(&store);
    store_text_actor();
    actor_range();
    store_edit_tracks();
    puts("actor data passed");
    return 0;
}
