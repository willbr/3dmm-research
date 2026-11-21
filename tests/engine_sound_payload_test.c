#include "mm/midi.h"
#include "mm/audioman.h"
#include "mm/wave.h"
#include "mm/wave_import.h"
#include "mm/sound_payload.h"
#include "engine_resource_block_fixture.h"
#include "chunk_writer_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct Fault
{
    int calls, fail, live;
} Fault;
static void *resize(void *p, void *old, MMU32 was, MMU32 bytes, int priority)
{
    Fault *f = (Fault *)p;
    void *result;
    (void)was;
    (void)priority;
    if (++f->calls == f->fail)
        return NULL;
    result = realloc(old, bytes);
    if (result && !old)
        ++f->live;
    return result;
}
static void release(void *p, void *data, MMU32 bytes)
{
    Fault *f = (Fault *)p;
    (void)bytes;
    --f->live;
    free(data);
}
static void midi(void)
{
    static const unsigned char raw[] = {0, 0x90, 60,   64, 5,    61, 63, 0, 0xff, 0x51, 3,
                                        3, 0xd0, 0x90, 7,  0x80, 61, 0,  0, 0xff, 0x2f, 0};
    static const unsigned char file[] = {'M', 'T', 'h', 'd',  0,   0,   0, 6,    0,    0,  0, 1,
                                         0,   100, 'M', 'T',  'r', 'k', 0, 0,    0,    12, 0, 0x90,
                                         60,  64,  100, 0x80, 60,  0,   0, 0xff, 0x2f, 0};
    static const unsigned char expected[] = {0, 0x90, 60, 64, 0x83, 0x74, 0x80, 60, 0};
    MMMidiParser p;
    MMMidiEvent e, peek;
    MMCollectionStorage scratch;
    MMStorageAllocator a;
    MMU32 bytes, n;
    unsigned char encoded[5], copy[sizeof(file)];
    int fail, i, ok;
    Fault f;
    mm_midi_parser_init(&p, raw, sizeof(raw), 0xfffffff0UL, 500000);
    assert(mm_midi_next(&p, &e, 0) && p.cursor == 0 && p.status == 0x90 && e.timestamp == 0xfffffff0UL);
    assert(mm_midi_next(&p, &peek, 1) && !memcmp(&e, &peek, sizeof(e)));
    assert(mm_midi_next(&p, &e, 1) && e.timestamp == 0xfffffff5UL && e.count == 3 && e.send[0] == 0x90 &&
           e.send[1] == 61);
    assert(mm_midi_next(&p, &e, 0) && !e.count && e.tempo == 250000 && p.tempo == 500000);
    assert(mm_midi_next(&p, &e, 1) && p.tempo == 250000);
    assert(mm_midi_next(&p, &e, 1) && e.timestamp == 0xfffffffcUL);
    assert(!mm_midi_next(&p, &e, 0) && p.cursor == p.size);
    assert(mm_midi_encode_time(0xffffffffUL, encoded) == 5 && encoded[0] == 0x8f && encoded[4] == 0x7f);
    for (n = 0; n < sizeof(raw); ++n)
    {
        mm_midi_parser_init(&p, raw, n, 0, 500000);
        for (i = 0; mm_midi_next(&p, &e, 1); ++i)
            assert(i < 10);
        assert(p.cursor == n);
    }
    /* SysEx length zero is rejected, matching the native parser. */
    encoded[0] = 0;
    encoded[1] = 0xf0;
    encoded[2] = 0;
    mm_midi_parser_init(&p, encoded, 3, 0, 500000);
    assert(!mm_midi_next(&p, &e, 1));
    for (fail = 0; fail < 12; ++fail)
    {
        memset(&f, 0, sizeof(f));
        f.fail = fail;
        a.context = &f;
        a.resize = resize;
        a.release = release;
        assert(mm_storage_init(&scratch, &a) == MM_STORAGE_OK);
        bytes = 99;
        ok = mm_midi_import(file, sizeof(file), &scratch, &bytes);
        if (ok)
            assert(bytes == sizeof(expected) && !memcmp(scratch.buffers[0].data, expected, bytes));
        else
            assert(bytes == 99);
        assert(mm_storage_dispose(&scratch) == MM_STORAGE_OK && !f.live);
    }
    mm_storage_init(&scratch, NULL);
    for (n = 0; n < sizeof(file); ++n)
    {
        bytes = 99;
        assert(!mm_midi_import(file, n, &scratch, &bytes) && bytes == 99);
    }
    memcpy(copy, file, sizeof(file));
    copy[12] = copy[13] = 0;
    assert(!mm_midi_import(copy, sizeof(copy), &scratch, &bytes));
    copy[13] = 100;
    copy[18] = 0x80;
    assert(!mm_midi_import(copy, sizeof(copy), &scratch, &bytes));
    mm_storage_dispose(&scratch);
}
typedef struct Import
{
    int fail, loads, converts, releases;
    unsigned char samples[1501];
} Import;
static int load(void *p, const void *data, MMU32 bytes, MMU32 *decoded)
{
    Import *i = (Import *)p;
    (void)data;
    (void)bytes;
    ++i->loads;
    *decoded = i->fail == 2 ? 0 : 1501;
    return i->fail != 1;
}
static int convert(void *p, unsigned char **samples, MMI32 *count)
{
    Import *i = (Import *)p;
    ++i->converts;
    *samples = i->samples;
    *count = i->fail == 4 ? 0 : 1501;
    return i->fail != 3;
}
static void samples_release(void *p, unsigned char *samples)
{
    Import *i = (Import *)p;
    assert(samples == i->samples);
    ++i->releases;
}
static void wave(void)
{
    unsigned char samples[1501], encoded[12000], saved[12000];
    MMU32 size = 123, frames = 99, n;
    MMWaveView view;
    MMWaveBuffer scratch;
    MMWaveImportServices services;
    const unsigned char *selected;
    MMU32 selected_bytes;
    Import i;
    int fail;
    memset(samples, 128, sizeof(samples));
    memset(encoded, 42, sizeof(encoded));
    assert(!mm_wave_encode(samples, 1, 0, 1, 8, 0, encoded, sizeof(encoded), &size) && size == 123 &&
           encoded[0] == 42);
    assert(!mm_wave_encode(samples, 1, 11025, 1, 8, 0, encoded, 44, &size) && size == 123 && encoded[0] == 42);
    assert(!mm_wave_encode(samples, 1, 0x7fffffffUL, 2, 16, 0, encoded, sizeof(encoded), &size));
    assert(mm_wave_encode(samples, 1501, 11025, 1, 8, 1, encoded, sizeof(encoded), &size) && size == 1114);
    assert(mm_wave_legacy_format(encoded, size));
    assert(mm_wave_fact_frames(encoded, size, &frames) && frames == 1501);
    assert(mm_wave_parse(encoded, size, &view) && view.format_bytes == 50 && view.data_bytes == 1024 &&
           view.fact == 1501);
    assert(view.data[0] == 0 && view.data[1] == 16 && view.data[2] == 0);
    memcpy(saved, encoded, size);
    for (n = 0; n < size; ++n)
    {
        memset(&view, 0x33, sizeof(view));
        assert(!mm_wave_parse(encoded, n, &view) && view.fact == 0x33333333);
    }
    encoded[4] = encoded[5] = encoded[6] = encoded[7] = 255;
    assert(!mm_wave_legacy_format(encoded, size) && !mm_wave_fact_frames(encoded, size, &frames));
    memcpy(encoded, saved, size);
    assert(mm_wave_encode(samples, 1, 11025, 1, 8, 0, encoded, sizeof(encoded), &size) && size == 46);
    /* Last odd chunk may omit its pad for compressed-reader compatibility. */
    encoded[4] = 37;
    assert(mm_wave_parse(encoded, 45, &view) && view.data_bytes == 1);
    encoded[4] = 38;
    services.context = &i;
    services.load = load;
    services.convert_u8 = convert;
    services.release_samples = samples_release;
    mm_wave_buffer_init(&scratch, NULL);
    memset(&i, 0, sizeof(i));
    assert(mm_wave_prepare(encoded, 46, &services, &scratch, &selected, &selected_bytes));
    assert(selected == encoded && selected_bytes == 46 && i.loads == 1 && !i.converts);
    for (fail = 0; fail < 5; ++fail)
    {
        memset(&i, 0, sizeof(i));
        memset(i.samples, 128, sizeof(i.samples));
        i.fail = fail;
        selected = encoded;
        selected_bytes = 77;
        if (!fail)
        {
            assert(mm_wave_prepare(saved, 1114, &services, &scratch, &selected, &selected_bytes));
            assert(selected == saved && selected_bytes == 1114);
        }
        else
            assert(!mm_wave_prepare(saved, 1114, &services, &scratch, &selected, &selected_bytes) &&
                   selected == encoded && selected_bytes == 77);
        assert(i.releases == (fail == 1 || fail == 2 ? 0 : 1));
    }
    mm_wave_buffer_dispose(&scratch);
}
#define FACTORY_PAYLOAD_BYTES 88
typedef struct Factory
{
    int fail, step, live, invalid, decoded, packed;
    int object, stream;
    void *midi;
    ResourceTestBlock block;
} Factory;
static int step(Factory *f)
{
    return ++f->step != f->fail;
}
static int unpack(void *p, void **handle)
{
    unsigned char expanded[FACTORY_PAYLOAD_BYTES];
    assert(resource_handle_size(NULL, *handle) == 77);
    if (!step((Factory *)p))
        return 0;
    memset(expanded, 0x61, sizeof(expanded));
    resource_block_decode(handle, expanded, sizeof(expanded));
    return 1;
}
static int factory_read(void *p, void *file, MMU32 offset, void *out, size_t bytes)
{
    assert(p == file && offset == 47 && (bytes == 77 || bytes == FACTORY_PAYLOAD_BYTES));
    if (!step((Factory *)p))
        return 0;
    memset(out, 0x61, bytes);
    return 1;
}
static int factory_write(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    (void)p; (void)file; (void)offset; (void)data; (void)bytes;
    assert(0);
    return 0;
}
static void *create(void *p)
{
    Factory *f = (Factory *)p;
    if (!step(f))
        return NULL;
    ++f->live;
    return &f->object;
}
static void object_release(void *p, void *o)
{
    Factory *f = (Factory *)p;
    assert(o == &f->object);
    resource_handle_release(NULL, f->midi);
    f->midi = NULL;
    --f->live;
}
static void adopt_midi(void *p, void *o, void *handle)
{
    Factory *f = (Factory *)p;
    assert(o == &f->object && !f->midi && handle && resource_handle_size(NULL, handle) == FACTORY_PAYLOAD_BYTES);
    assert(*(unsigned char *)resource_handle_lock(NULL, handle) == 0x61);
    f->midi = handle;
}
static void invalidate(void *p, void *out, size_t bytes)
{
    ++((Factory *)p)->invalid;
    memset(out, 0x55, bytes);
}
static int decode(void *p, void *o, const void *encoded, MMU32 bytes)
{
    const unsigned char *data = (const unsigned char *)encoded;
    assert(o == &((Factory *)p)->object && bytes == FACTORY_PAYLOAD_BYTES);
    assert(data[0] == 0x61 && data[bytes - 1] == 0x61);
    return step((Factory *)p);
}
static int allocation_size(void *p, void *o, MMI32 *bytes)
{
    (void)o;
    *bytes = 1000;
    return step((Factory *)p);
}
static void decoded(void *p)
{
    ++((Factory *)p)->decoded;
}
static void *open_stream(void *p, MMOwnedBlock *block)
{
    Factory *f = (Factory *)p;
    assert(block->file == p && block->view.start == 47 && block->view.packed == f->packed);
    assert(block->view.length == (f->packed ? 77 : FACTORY_PAYLOAD_BYTES));
    if (!step(f))
        return NULL;
    ++f->live;
    return &f->stream;
}
static void release_stream(void *p, void *stream)
{
    Factory *f = (Factory *)p;
    assert(stream == &f->stream);
    --f->live;
}
static MMI32 stream_size(void *p, void *stream)
{
    (void)p;
    (void)stream;
    return 101;
}
static int decode_stream(void *p, void *o, void *stream)
{
    (void)o;
    (void)stream;
    return step((Factory *)p);
}
static void adopt(void *p, void *o, void *stream)
{
    assert(o == &((Factory *)p)->object);
    release_stream(p, stream);
}
static void factories(void)
{
    MMSoundPayloadServices s;
    Factory f;
    MMBlockFile source;
    void *object;
    MMI32 bytes;
    int kind, fail, ok, test, packed, steps;
    memset(&s, 0, sizeof(s));
    s.context = &f;
    s.object_bytes = 42;
    s.decode_block = unpack;
    s.create = create;
    s.release = object_release;
    s.adopt_midi = adopt_midi;
    s.invalidate = invalidate;
    s.decode = decode;
    s.allocation_size = allocation_size;
    s.decoded = decoded;
    s.open_stream = open_stream;
    s.release_stream = release_stream;
    s.stream_size = stream_size;
    s.decode_stream = decode_stream;
    s.adopt_stream = adopt;
    source.file = &f;
    source.offset = 47;
    for (test = 0; test < 6; ++test)
    {
        kind = test / 2;
        packed = test % 2;
        steps = kind == 0 ? 2 + packed : kind == 1 ? 4 + packed : 3;
        for (fail = 0; fail <= steps; ++fail)
        {
            memset(&f, 0, sizeof(f));
            f.packed = packed;
            source.length = packed ? 77 : FACTORY_PAYLOAD_BYTES;
            resource_block_init(&f.block, &f, factory_read, factory_write);
            assert(mm_owned_block_set_file(&f.block.owner, &source, packed) == MM_DATA_OK);
            f.fail = fail;
            bytes = 0;
            object = NULL;
            if (!kind)
                assert(mm_midi_payload_fetch(&f.block.owner, NULL, &bytes, &s) && bytes == (MMI32)source.length);
            else if (kind == 1)
                assert(mm_wave_payload_fetch(&f.block.owner, NULL, &bytes, &s) && bytes == 42);
            else
                assert(mm_wave_stream_fetch(&f.block.owner, NULL, &bytes, &s) && bytes == 42);
            assert(!f.step);
            if (!kind)
                ok = mm_midi_payload_fetch(&f.block.owner, &object, &bytes, &s);
            else if (kind == 1)
                ok = mm_wave_payload_fetch(&f.block.owner, &object, &bytes, &s);
            else
                ok = mm_wave_stream_fetch(&f.block.owner, &object, &bytes, &s);
            assert(ok == (fail == 0) && f.step == (fail ? fail : steps));
            assert(f.decoded == (kind == 1 && ok));
            if (ok)
            {
                assert(object == &f.object && bytes == (kind == 0 ? FACTORY_PAYLOAD_BYTES : kind == 1 ? 1000 : 101));
                object_release(&f, object);
            }
            else if (!kind)
                assert(f.invalid == 2);
            else
                assert(!object && bytes == (kind == 2 && fail > 1 ? 101 : 42));
            assert(!f.live);
            assert(mm_owned_block_clear(&f.block.owner) == MM_DATA_OK);
        }
    }
    memset(&f, 0, sizeof(f));
    source.length = 11;
    resource_block_init(&f.block, &f, factory_read, factory_write);
    assert(!mm_wave_stream_fetch(&f.block.owner, &object, &bytes, &s) && !object && bytes == 42 && !f.step);
    assert(mm_owned_block_set_file(&f.block.owner, &source, 0) == MM_DATA_OK);
    assert(!mm_wave_payload_fetch(&f.block.owner, &object, &bytes, &s) && !object && bytes == 42 && !f.step);
    assert(mm_owned_block_clear(&f.block.owner) == MM_DATA_OK);
}

typedef struct StreamBacking
{
    unsigned char source[16], temp[16];
    int source_refs, temp_refs, fail_read, fail_write, fail_temp;
} StreamBacking;
static const unsigned char backing_payload[4] = {'W', 'A', 'V', 'E'};

static void backing_retain(void *p, void *file)
{
    StreamBacking *b = p;
    assert(file == b->source || file == b->temp);
    ++*(file == b->source ? &b->source_refs : &b->temp_refs);
}

static void backing_release(void *p, void *file)
{
    StreamBacking *b = p;
    int *refs = file == b->source ? &b->source_refs : &b->temp_refs;
    assert((file == b->source || file == b->temp) && *refs > 0);
    --*refs;
}

static void *backing_temp(void *p)
{
    StreamBacking *b = p;
    assert(!b->temp_refs);
    if (b->fail_temp)
        return NULL;
    b->temp_refs = 1;
    return b->temp;
}

static void *no_backing_temp(void *p)
{
    (void)p;
    return NULL;
}

static int backing_read(void *p, void *file, MMU32 offset, void *out, size_t bytes)
{
    StreamBacking *b = p;
    assert((file == b->source && b->source_refs) || (file == b->temp && b->temp_refs));
    assert(offset <= sizeof(b->source) && bytes <= sizeof(b->source) - offset);
    if (b->fail_read)
        return 0;
    memcpy(out, (unsigned char *)file + offset, bytes);
    return 1;
}

static int backing_write(void *p, void *file, MMU32 offset, const void *in, size_t bytes)
{
    StreamBacking *b = p;
    assert(file == b->temp && b->temp_refs && !offset && bytes <= sizeof(b->temp));
    if (b->fail_write)
        return 0;
    memcpy(b->temp, in, bytes);
    return 1;
}

static int backing_decode(void *p, void **handle)
{
    const unsigned char *source = resource_handle_lock(NULL, *handle);
    (void)p;
    assert(resource_handle_size(NULL, *handle) == 9);
    if (!source[0])
        return 0;
    resource_block_decode(handle, backing_payload, sizeof(backing_payload));
    return 1;
}

static void stream_backing(void)
{
    static const struct {
        int packed, removable, limit, fail_read, fail_write, fail_temp, fail_decode, expected;
    } cases[] = {
        {0, 0, 1, 0, 0, 0, 0, MM_BLOCK_FILE},   /* Fixed disk remains file-backed. */
        {0, 1, 8, 0, 0, 0, 0, MM_BLOCK_MEMORY}, /* Exactly at the limit. */
        {0, 1, 7, 0, 0, 0, 0, MM_BLOCK_FILE},
        {0, 1, 7, 0, 1, 0, 0, MM_BLOCK_EMPTY},
        {0, 1, 8, 1, 0, 0, 0, MM_BLOCK_EMPTY},
        {1, 0, 8, 0, 0, 0, 0, MM_BLOCK_MEMORY},
        {1, 0, 7, 0, 0, 0, 0, MM_BLOCK_FILE},
        {1, 0, 7, 0, 1, 0, 0, MM_BLOCK_MEMORY},
        {1, 0, 7, 0, 0, 1, 0, MM_BLOCK_MEMORY},
        {1, 0, 8, 0, 0, 0, 1, MM_BLOCK_EMPTY}
    };
    StreamBacking b;
    ResourceTestBlock root;
    MMAudioManStream stream;
    MMBlockFileServices temporary;
    MMOwnedBlock *dest = &stream.block;
    MMBlockFile extent;
    static const MMWide positive_limit = {0xffffffffUL, 2147483647};
    static const MMWide negative_limit = {0, -2147483647 - 1};
    unsigned i;
    unsigned char payload[4];
    MMU32 count, position;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        memset(&b, 0, sizeof(b));
        memcpy(b.source + 3, backing_payload, sizeof(backing_payload));
        b.fail_read = cases[i].fail_read;
        b.fail_write = cases[i].fail_write;
        b.fail_temp = cases[i].fail_temp;
        if (cases[i].fail_decode)
            b.source[3] = 0;
        resource_block_init(&root, &b, backing_read, backing_write);
        root.files.retain = backing_retain;
        root.files.release = backing_release;
        root.files.create_temp = backing_temp;
        temporary = root.files;
        root.files.create_temp = no_backing_temp;
        assert(mm_audioman_stream_init(&stream, root.owner.handles, &temporary) == MM_DATA_OK);
        extent.file = b.source;
        extent.offset = 3;
        extent.length = cases[i].packed ? 9 : 4;
        assert(mm_owned_block_set_file(&root.owner, &extent, cases[i].packed) == MM_DATA_OK);
        assert(mm_wave_stream_prepare(&root.owner, dest, cases[i].removable, 4, cases[i].limit, backing_decode,
                                       NULL) == (cases[i].expected != MM_BLOCK_EMPTY));
        assert(root.owner.file == b.source && root.owner.view.packed == cases[i].packed && root.owner.view.start == 3);
        assert(mm_owned_block_clear(&root.owner) == MM_DATA_OK);
        assert(dest->view.kind == cases[i].expected && !dest->view.packed);
        assert(dest->files == (!cases[i].packed && !cases[i].removable ? &root.files : &temporary));
        assert(mm_audioman_stream_resident(&stream) == (cases[i].expected == MM_BLOCK_MEMORY ? 4U : 0U));
        if (dest->view.kind != MM_BLOCK_EMPTY)
        {
            assert(mm_audioman_stream_seek(&stream, mm_wide_i32(-1), MM_AUDIOMAN_SEEK_END, &position) == MM_DATA_OK &&
                   position == 3);
            assert(mm_audioman_stream_read(&stream, payload, 0xffffffffUL, &count) == MM_DATA_OK &&
                   count == 1 && payload[0] == backing_payload[3]);
            assert(mm_audioman_stream_read(&stream, NULL, 4, &count) == MM_DATA_OK && !count);
            assert(mm_audioman_stream_seek(&stream, positive_limit, MM_AUDIOMAN_SEEK_CUR, &position) ==
                       MM_DATA_ARGUMENT && position == 4);
            assert(mm_audioman_stream_seek(&stream, negative_limit, MM_AUDIOMAN_SEEK_END, &position) ==
                       MM_DATA_ARGUMENT && position == 4);
            assert(mm_audioman_stream_seek(&stream, mm_wide_i32(-5), MM_AUDIOMAN_SEEK_CUR, NULL) == MM_DATA_ARGUMENT);
            assert(mm_audioman_stream_seek(&stream, mm_wide_i32(-4), MM_AUDIOMAN_SEEK_CUR, &position) == MM_DATA_OK &&
                   !position);
            if (dest->view.kind == MM_BLOCK_FILE)
            {
                b.fail_read = 1;
                assert(mm_audioman_stream_read(&stream, payload, 4, &count) == MM_DATA_IO && !count && !stream.position);
                b.fail_read = 0;
            }
            assert(mm_audioman_stream_read(&stream, payload, 4, &count) == MM_DATA_OK && count == sizeof(payload) &&
                   !memcmp(payload, backing_payload, sizeof(payload)));
            assert(mm_audioman_stream_seek(&stream, mm_wide_i32(1), 99, &position) == MM_DATA_OK && position == 1);
        }
        mm_audioman_stream_dispose(&stream);
        assert(!stream.position && !b.source_refs && !b.temp_refs);
    }
}

typedef struct WaveFile
{
    ChunkWriterFixture archive;
    int fail, step, bad, cleanup, deleted, opens_memory, force_memory;
    MMI32 style;
    MMU32 source_bytes, encoded_bytes, length, written;
    unsigned char source[1300], encoded[1100], bytes[1300];
    const unsigned char *borrowed;
} WaveFile;
static int file_step(WaveFile *f) { return ++f->step != f->fail; }
static void file_init(WaveFile *f, MMI32 style, MMU32 length)
{
    MMU32 i;
    memset(f, 0, sizeof(*f));
    f->style = style;
    f->source_bytes = length;
    f->encoded_bytes = 30;
    for (i = 0; i < length; ++i) f->source[i] = (unsigned char)(i * 3 + 9);
    for (i = 0; i < sizeof(f->encoded); ++i) f->encoded[i] = (unsigned char)(i * 5);
}
static int read_source(void *p, MMU32 offset, void *bytes, size_t count)
{
    WaveFile *f = (WaveFile *)p;
    assert(offset <= f->source_bytes && count <= f->source_bytes - offset);
    memcpy(bytes, f->source + offset, count);
    return file_step(f);
}
static int allocate_sound(void *p, MMU32 bytes, MMChunkLocation *out)
{
    WaveFile *f = ((ChunkIndexFixture *)p)->observer;
    int child = f->archive.index.count == 3;
    assert(child ? bytes <= sizeof(f->bytes) : bytes == MM_SOUND_HEADER_BYTES);
    if (!file_step(f))
        return 0;
    out->file = &f->archive;
    out->offset = child ? 256 : 128;
    out->bytes = bytes;
    out->flags = 0;
    if (child)
        f->length = bytes;
    return 1;
}
static int write_sound(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{
    ChunkWriterFixture *archive = p;
    WaveFile *f = archive->context;
    const unsigned char *header = bytes;
    assert(file == p);
    if (offset == 128)
    {
        assert(count == MM_SOUND_HEADER_BYTES && header[4] == f->style && !header[12]);
        return 1;
    }
    assert(archive->file_refs == 1 && offset == 256 + f->written && count <= f->length - f->written);
    if (!file_step(f)) return 0;
    memcpy(f->bytes + f->written, bytes, count);
    f->written += (MMU32)count;
    return 1;
}
static int set_name(void *p)
{
    WaveFile *f = (WaveFile *)p;
    assert(f->written == f->length);
    return file_step(f);
}
static void delete_parent(void *p, const MMChunkEntry *entry)
{
    if (entry->type == MM_SOUND_TYPE)
        ++((WaveFile *)p)->deleted;
}
static MMArchiveWriter file_writer(WaveFile *f)
{
    MMArchiveWriter writer = chunk_writer_fixture(&f->archive, f, allocate_sound, write_sound);
    f->archive.grow_name = set_name;
    f->archive.index.before_delete = delete_parent;
    fixture_chunk_add(&f->archive.index, 1, 0);
    return writer;
}
static const MMChunkNameValue wave_name = {"wave", 4, 4};
static void file_bad(void *p) { ++((WaveFile *)p)->bad; }
static void native_format(MMNativeWaveFormat *format)
{
    format->rate = 44100;
    format->bits = 16;
    format->channels = 2;
}
static int open_sound(void *p, MMNativeWaveFormat *format)
{
    WaveFile *f = (WaveFile *)p;
    native_format(format);
    return file_step(f) && !f->force_memory;
}
static int open_memory(void *p, unsigned char *bytes, MMU32 count, MMNativeWaveFormat *format)
{
    WaveFile *f = (WaveFile *)p;
    assert(count == f->source_bytes && !memcmp(f->source, bytes, count));
    f->borrowed = bytes;
    ++f->opens_memory;
    native_format(format);
    return file_step(f);
}
static int native_convert(void *p) { return file_step((WaveFile *)p); }
static MMU32 sample_count(void *p)
{
    (void)p;
    return 2000;
}
static int activate(void *p) { return file_step((WaveFile *)p); }
static int allocate_samples(void *p, MMU32 n)
{
    assert(n == 2000);
    return file_step((WaveFile *)p);
}
static int read_samples(void *p, MMU32 *n)
{
    assert(*n == 2000);
    return file_step((WaveFile *)p);
}
static int compress(void *p, MMU32 n, const void **format, MMU32 *fs, const void **data, MMU32 *ds)
{
    static const unsigned char f[18] = {1, 2, 3, 4};
    WaveFile *v = (WaveFile *)p;
    assert(n == 2000);
    *format = f;
    *fs = sizeof(f);
    *data = v->encoded;
    *ds = v->encoded_bytes;
    return file_step(v) ? 1 : -1;
}
static void cleanup(void *p)
{
    WaveFile *f = (WaveFile *)p;
    if (f->borrowed)
        assert(!memcmp(f->borrowed, f->source, f->source_bytes));
    ++f->cleanup;
}
static int native_import(WaveFile *f, MMU32 *number, const MMNativeWaveServices *services,
                         const MMStorageAllocator *transfer)
{
    MMArchiveWriter writer = file_writer(f);
    MMReadStream source = {NULL, 0, read_source};
    writer.transfer_allocator = transfer;
    source.context = f;
    source.length = f->source_bytes;
    return mm_wave_native_import(&source, f->style, &wave_name, number, services, &writer);
}
static void native(void)
{
    WaveFile f;
    MMNativeWaveServices services = {NULL, NULL, open_sound, NULL, native_convert, sample_count, activate,
                                     allocate_samples, read_samples, compress, file_bad, cleanup};
    Fault allocation = {0};
    MMStorageAllocator allocator = {NULL, resize, release};
    MMU32 number;
    int fail, total;
    services.context = &f;
    file_init(&f, 2, 85);
    number = 9;
    assert(native_import(&f, &number, &services, NULL));
    /* Preserve the classic comparison of the RIFF payload (80 bytes) with the
     * original file (85 bytes), even though the whole encoded file is 88 bytes. */
    assert(number == 0 && f.written == 88 && f.bytes[4] == 80 && f.cleanup == 1 && !f.deleted);
    assert(!memcmp(f.bytes, "RIFF", 4) && !memcmp(f.bytes + 12, "fmt ", 4) &&
           !memcmp(f.bytes + 38, "fact", 4) && !memcmp(f.bytes + 50, "data", 4));
    assert(!memcmp(f.bytes + 58, f.encoded, f.encoded_bytes));
    total = f.step;
    for (fail = 1; fail <= total; ++fail)
    {
        file_init(&f, 2, 85);
        f.fail = fail;
        number = 9;
        assert(!native_import(&f, &number, &services, NULL) && f.cleanup == 1);
        assert(number == (fail > 7 ? 0 : 9) && f.deleted == (fail > 7));
        assert(f.archive.index.count == 1 && !f.archive.file_refs);
        assert(f.bad == (fail == 1 || fail == 2 || fail == 3 || fail == 5 || fail == 6));
    }
    file_init(&f, 2, 70);
    assert(native_import(&f, &number, &services, NULL));
    assert(f.written == f.source_bytes && !memcmp(f.bytes, f.source, f.source_bytes));

    allocator.context = &allocation;
    services.allocator = &allocator;
    services.open_memory = open_memory;
    for (fail = 0; fail <= 3; ++fail)
    {
        file_init(&f, 2, 85);
        f.force_memory = 1;
        f.fail = fail;
        number = 9;
        allocation.fail = fail == 1;
        allocation.calls = 0;
        assert(native_import(&f, &number, &services, NULL) == !fail);
        assert(f.cleanup == 1 && !allocation.live && f.opens_memory == (fail == 0 || fail == 3));
        assert(f.bad == (fail >= 2));
    }

    /* Force the writer's bounded transfer path to start a new read inside the
     * encoded data after its first 1 KiB. */
    file_init(&f, 2, sizeof(f.source));
    f.encoded_bytes = sizeof(f.encoded);
    allocation.calls = 0;
    allocation.fail = 1;
    assert(native_import(&f, &number, &services, &allocator));
    assert(f.written == f.encoded_bytes + 58 && !memcmp(f.bytes + 58, f.encoded, f.encoded_bytes));
    assert(allocation.calls == 1 && !allocation.live);
}
static int physical_prepare(void *p, const void *data, MMU32 bytes, const void **out, MMU32 *count)
{
    *out = data;
    *count = bytes;
    return file_step((WaveFile *)p);
}
static void physical(void)
{
    WaveFile f;
    MMWaveFileServices s = {NULL, NULL, physical_prepare, file_bad};
    MMArchiveWriter writer;
    MMReadStream source = {NULL, 12, read_source};
    MMStorageAllocator a;
    MMWaveBuffer b;
    MMWaveImportServices ws;
    MMU32 number, size;
    const unsigned char *selected;
    unsigned char wave[44] = {0};
    Fault fault;
    Import input;
    int fail, ok;
    source.context = &f;
    s.context = &f;
    for (fail = 0; fail <= 6; ++fail)
    {
        file_init(&f, 3, 12);
        writer = file_writer(&f);
        f.fail = fail;
        number = 7;
        ok = mm_wave_file_import(&source, 3, &wave_name, &number, &s, &writer);
        assert(ok == !fail && f.bad == (fail == 1 || fail == 2));
        assert(number == (fail == 0 || fail >= 4 ? 0 : 7) && f.deleted == (fail >= 4));
        assert(f.archive.index.count == (!fail ? 3U : 1U) && !f.archive.file_refs);
        if (ok) assert(!memcmp(f.bytes, f.source, source.length));
    }
    memset(&fault, 0, sizeof(fault));
    fault.fail = 1;
    a.context = &fault;
    a.resize = resize;
    a.release = release;
    file_init(&f, 3, 12);
    writer = file_writer(&f);
    s.allocator = &a;
    assert(!mm_wave_file_import(&source, 3, &wave_name, &number, &s, &writer) && !fault.live && !f.step && !f.bad);
    /* C encoding failure releases converted host samples and preserves outputs. */
    memset(&input, 0, sizeof(input));
    memset(&fault, 0, sizeof(fault));
    fault.fail = 1;
    ws.context = &input;
    ws.load = load;
    ws.convert_u8 = convert;
    ws.release_samples = samples_release;
    assert(mm_wave_buffer_init(&b, &a));
    selected = wave;
    size = 77;
    assert(!mm_wave_prepare(wave, sizeof(wave), &ws, &b, &selected, &size));
    assert(input.releases == 1 && selected == wave && size == 77 && !fault.live);
    mm_wave_buffer_dispose(&b);
}
int main(void)
{
    midi();
    wave();
    factories();
    stream_backing();
    native();
    physical();
    puts("C89 sound payloads: parser/import, RIFF/ADPCM, factories and import failure cleanup passed.");
    return 0;
}
