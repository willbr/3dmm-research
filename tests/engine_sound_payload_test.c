#include "three_mm/midi.h"
#include "three_mm/wave.h"
#include "three_mm/wave_import.h"
#include "three_mm/sound_payload.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct Fault
{
    int calls, fail, live;
} Fault;
static void *resize(void *p, void *old, ThreeMMU32 was, ThreeMMU32 bytes, int priority)
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
static void release(void *p, void *data, ThreeMMU32 bytes)
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
    ThreeMMMidiParser p;
    ThreeMMMidiEvent e, peek;
    ThreeMMCollectionStorage scratch;
    ThreeMMStorageAllocator a;
    ThreeMMU32 bytes, n;
    unsigned char encoded[5], copy[sizeof(file)];
    int fail, i, ok;
    Fault f;
    three_mm_midi_parser_init(&p, raw, sizeof(raw), 0xfffffff0UL, 500000);
    assert(three_mm_midi_next(&p, &e, 0) && p.cursor == 0 && p.status == 0x90 && e.timestamp == 0xfffffff0UL);
    assert(three_mm_midi_next(&p, &peek, 1) && !memcmp(&e, &peek, sizeof(e)));
    assert(three_mm_midi_next(&p, &e, 1) && e.timestamp == 0xfffffff5UL && e.count == 3 && e.send[0] == 0x90 &&
           e.send[1] == 61);
    assert(three_mm_midi_next(&p, &e, 0) && !e.count && e.tempo == 250000 && p.tempo == 500000);
    assert(three_mm_midi_next(&p, &e, 1) && p.tempo == 250000);
    assert(three_mm_midi_next(&p, &e, 1) && e.timestamp == 0xfffffffcUL);
    assert(!three_mm_midi_next(&p, &e, 0) && p.cursor == p.size);
    assert(three_mm_midi_encode_time(0xffffffffUL, encoded) == 5 && encoded[0] == 0x8f && encoded[4] == 0x7f);
    for (n = 0; n < sizeof(raw); ++n)
    {
        three_mm_midi_parser_init(&p, raw, n, 0, 500000);
        for (i = 0; three_mm_midi_next(&p, &e, 1); ++i)
            assert(i < 10);
        assert(p.cursor == n);
    }
    /* SysEx length zero is rejected, matching the native parser. */
    encoded[0] = 0;
    encoded[1] = 0xf0;
    encoded[2] = 0;
    three_mm_midi_parser_init(&p, encoded, 3, 0, 500000);
    assert(!three_mm_midi_next(&p, &e, 1));
    for (fail = 0; fail < 12; ++fail)
    {
        memset(&f, 0, sizeof(f));
        f.fail = fail;
        a.context = &f;
        a.resize = resize;
        a.release = release;
        assert(three_mm_storage_init(&scratch, &a) == THREE_MM_STORAGE_OK);
        bytes = 99;
        ok = three_mm_midi_import(file, sizeof(file), &scratch, &bytes);
        if (ok)
            assert(bytes == sizeof(expected) && !memcmp(scratch.buffers[0].data, expected, bytes));
        else
            assert(bytes == 99);
        assert(three_mm_storage_dispose(&scratch) == THREE_MM_STORAGE_OK && !f.live);
    }
    three_mm_storage_init(&scratch, NULL);
    for (n = 0; n < sizeof(file); ++n)
    {
        bytes = 99;
        assert(!three_mm_midi_import(file, n, &scratch, &bytes) && bytes == 99);
    }
    memcpy(copy, file, sizeof(file));
    copy[12] = copy[13] = 0;
    assert(!three_mm_midi_import(copy, sizeof(copy), &scratch, &bytes));
    copy[13] = 100;
    copy[18] = 0x80;
    assert(!three_mm_midi_import(copy, sizeof(copy), &scratch, &bytes));
    three_mm_storage_dispose(&scratch);
}
typedef struct Import
{
    int fail, loads, converts, releases;
    unsigned char samples[1501];
} Import;
static int load(void *p, const void *data, ThreeMMU32 bytes, ThreeMMU32 *decoded)
{
    Import *i = (Import *)p;
    (void)data;
    (void)bytes;
    ++i->loads;
    *decoded = i->fail == 2 ? 0 : 1501;
    return i->fail != 1;
}
static int convert(void *p, unsigned char **samples, ThreeMMI32 *count)
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
    ThreeMMU32 size = 123, frames = 99, n;
    ThreeMMWaveView view;
    ThreeMMWaveBuffer scratch;
    ThreeMMWaveImportServices services;
    const unsigned char *selected;
    ThreeMMU32 selected_bytes;
    Import i;
    int fail;
    memset(samples, 128, sizeof(samples));
    memset(encoded, 42, sizeof(encoded));
    assert(!three_mm_wave_encode(samples, 1, 0, 1, 8, 0, encoded, sizeof(encoded), &size) && size == 123 &&
           encoded[0] == 42);
    assert(!three_mm_wave_encode(samples, 1, 11025, 1, 8, 0, encoded, 44, &size) && size == 123 && encoded[0] == 42);
    assert(!three_mm_wave_encode(samples, 1, 0x7fffffffUL, 2, 16, 0, encoded, sizeof(encoded), &size));
    assert(three_mm_wave_encode(samples, 1501, 11025, 1, 8, 1, encoded, sizeof(encoded), &size) && size == 1114);
    assert(three_mm_wave_legacy_format(encoded, size));
    assert(three_mm_wave_fact_frames(encoded, size, &frames) && frames == 1501);
    assert(three_mm_wave_parse(encoded, size, &view) && view.format_bytes == 50 && view.data_bytes == 1024 &&
           view.fact == 1501);
    assert(view.data[0] == 0 && view.data[1] == 16 && view.data[2] == 0);
    memcpy(saved, encoded, size);
    for (n = 0; n < size; ++n)
    {
        memset(&view, 0x33, sizeof(view));
        assert(!three_mm_wave_parse(encoded, n, &view) && view.fact == 0x33333333);
    }
    encoded[4] = encoded[5] = encoded[6] = encoded[7] = 255;
    assert(!three_mm_wave_legacy_format(encoded, size) && !three_mm_wave_fact_frames(encoded, size, &frames));
    memcpy(encoded, saved, size);
    assert(three_mm_wave_encode(samples, 1, 11025, 1, 8, 0, encoded, sizeof(encoded), &size) && size == 46);
    /* Last odd chunk may omit its pad for compressed-reader compatibility. */
    encoded[4] = 37;
    assert(three_mm_wave_parse(encoded, 45, &view) && view.data_bytes == 1);
    encoded[4] = 38;
    services.context = &i;
    services.load = load;
    services.convert_u8 = convert;
    services.release_samples = samples_release;
    three_mm_wave_buffer_init(&scratch, NULL);
    memset(&i, 0, sizeof(i));
    assert(three_mm_wave_prepare(encoded, 46, &services, &scratch, &selected, &selected_bytes));
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
            assert(three_mm_wave_prepare(saved, 1114, &services, &scratch, &selected, &selected_bytes));
            assert(selected == saved && selected_bytes == 1114);
        }
        else
            assert(!three_mm_wave_prepare(saved, 1114, &services, &scratch, &selected, &selected_bytes) &&
                   selected == encoded && selected_bytes == 77);
        assert(i.releases == (fail == 1 || fail == 2 ? 0 : 1));
    }
    three_mm_wave_buffer_dispose(&scratch);
}
typedef struct Factory
{
    int fail, step, live, invalid, decoded;
    int object, stream;
} Factory;
static int step(Factory *f)
{
    return ++f->step != f->fail;
}
static int unpack(void *p, void *block)
{
    (void)block;
    return step((Factory *)p);
}
static ThreeMMI32 block_size(void *p, void *b, int packed)
{
    (void)p;
    (void)b;
    return packed ? 77 : 88;
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
    assert(o == &f->object || o == &f->stream);
    --f->live;
}
static int read_midi(void *p, void *o, void *b)
{
    (void)o;
    (void)b;
    return step((Factory *)p);
}
static void invalidate(void *p, void *out, size_t bytes)
{
    ++((Factory *)p)->invalid;
    memset(out, 0x55, bytes);
}
static int read_encoded(void *p, void *b, ThreeMMI32 bytes)
{
    (void)b;
    assert(bytes == 88);
    return step((Factory *)p);
}
static int decode(void *p, void *o)
{
    (void)o;
    return step((Factory *)p);
}
static int allocation_size(void *p, void *o, ThreeMMI32 *bytes)
{
    (void)o;
    *bytes = 1000;
    return step((Factory *)p);
}
static void decoded(void *p)
{
    ++((Factory *)p)->decoded;
}
static void *open_stream(void *p, void *cache, ThreeMMU32 type, ThreeMMU32 number)
{
    Factory *f = (Factory *)p;
    (void)cache;
    (void)type;
    (void)number;
    if (!step(f))
        return NULL;
    ++f->live;
    return &f->stream;
}
static ThreeMMI32 stream_size(void *p, void *stream)
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
    (void)o;
    object_release(p, stream);
}
static void factories(void)
{
    ThreeMMSoundPayloadServices s;
    Factory f;
    void *object;
    ThreeMMI32 bytes;
    int kind, fail, ok;
    memset(&s, 0, sizeof(s));
    s.context = &f;
    s.object_bytes = 42;
    s.unpack = unpack;
    s.block_size = block_size;
    s.create = create;
    s.release = object_release;
    s.read_midi = read_midi;
    s.invalidate = invalidate;
    s.read_encoded = read_encoded;
    s.decode = decode;
    s.allocation_size = allocation_size;
    s.decoded = decoded;
    s.open_stream = open_stream;
    s.stream_size = stream_size;
    s.decode_stream = decode_stream;
    s.adopt_stream = adopt;
    for (kind = 0; kind < 3; ++kind)
        for (fail = 0; fail < 8; ++fail)
        {
            memset(&f, 0, sizeof(f));
            f.fail = fail;
            bytes = 0;
            object = NULL;
            if (!kind)
                ok = three_mm_midi_payload_fetch(NULL, &object, &bytes, &s);
            else if (kind == 1)
                ok = three_mm_wave_payload_fetch(NULL, &object, &bytes, &s);
            else
                ok = three_mm_wave_stream_fetch(NULL, 1, 2, &object, &bytes, &s);
            if (ok)
            {
                assert(object == &f.object && bytes == (kind == 0 ? 88 : kind == 1 ? 1000 : 101));
                object_release(&f, object);
            }
            else if (!kind)
                assert(f.invalid == 2);
            else
                assert(!object);
            assert(!f.live);
            memset(&f, 0, sizeof(f));
            if (!kind)
                assert(three_mm_midi_payload_fetch(NULL, NULL, &bytes, &s) && bytes == 77);
            else if (kind == 1)
                assert(three_mm_wave_payload_fetch(NULL, NULL, &bytes, &s) && bytes == 42);
            else
                assert(three_mm_wave_stream_fetch(NULL, 1, 2, NULL, &bytes, &s) && bytes == 42);
            assert(!f.step);
        }
}
/* Every externally fallible native phase, including each RIFF write, is
 * faulted independently. Selection intentionally compares the payload length. */
typedef struct Native
{
    int fail, step, bad, cleanup, finish, original;
    unsigned char bytes[200];
    unsigned size;
} Native;
static int native_step(Native *n)
{
    return ++n->step != n->fail;
}
static int begin(void *p, void *source, const void *name)
{
    (void)source;
    (void)name;
    return native_step((Native *)p);
}
static int original_size(void *p, ThreeMMI32 *bytes)
{
    *bytes = 85;
    return native_step((Native *)p);
}
static int open_sound(void *p, ThreeMMNativeWaveFormat *format)
{
    format->rate = 44100;
    format->bits = 16;
    format->channels = 2;
    return native_step((Native *)p);
}
static int native_convert(void *p)
{
    return native_step((Native *)p);
}
static ThreeMMU32 sample_count(void *p)
{
    (void)p;
    return 100;
}
static int activate(void *p)
{
    return native_step((Native *)p);
}
static int allocate_samples(void *p, ThreeMMU32 n)
{
    assert(n == 100);
    return native_step((Native *)p);
}
static int read_samples(void *p, ThreeMMU32 *n)
{
    assert(*n == 100);
    return native_step((Native *)p);
}
static int compress(void *p, ThreeMMU32 n, const void **format, ThreeMMU32 *fs, const void **data, ThreeMMU32 *ds)
{
    static const unsigned char f[18] = {0}, d[30] = {0};
    assert(n == 100);
    *format = f;
    *fs = 18;
    *data = d;
    *ds = 30;
    return native_step((Native *)p) ? 1 : -1;
}
static int create_file(void *p)
{
    return native_step((Native *)p);
}
static int write_file(void *p, const void *data, ThreeMMU32 n)
{
    Native *v = (Native *)p;
    if (!native_step(v))
        return 0;
    assert(v->size + n <= sizeof(v->bytes));
    memcpy(v->bytes + v->size, data, n);
    v->size += n;
    return 1;
}
static void finish(void *p)
{
    ++((Native *)p)->finish;
}
static int selected_file(void *p, int original)
{
    Native *n = (Native *)p;
    n->original = original;
    return native_step(n);
}
static int write_resource(void *p, void *dest, ThreeMMI32 style, void *number)
{
    (void)dest;
    assert(style == 2);
    *(ThreeMMU32 *)number = 123;
    return native_step((Native *)p);
}
static void bad(void *p)
{
    ++((Native *)p)->bad;
}
static void cleanup(void *p)
{
    ++((Native *)p)->cleanup;
}
static void native(void)
{
    ThreeMMNativeWaveServices s;
    Native n;
    ThreeMMU32 number;
    int fail, ok, total;
    memset(&s, 0, sizeof(s));
    s.context = &n;
    s.begin = begin;
    s.original_size = original_size;
    s.open_sound = open_sound;
    s.convert = native_convert;
    s.samples = sample_count;
    s.activate = activate;
    s.allocate_samples = allocate_samples;
    s.read_samples = read_samples;
    s.compress = compress;
    s.create_file = create_file;
    s.write = write_file;
    s.finish_conversion = finish;
    s.open_selected = selected_file;
    s.write_resource = write_resource;
    s.bad_sound = bad;
    s.cleanup = cleanup;
    memset(&n, 0, sizeof(n));
    number = 9;
    assert(three_mm_wave_native_import(NULL, NULL, 2, NULL, &number, &s));
    assert(number == 123 && n.size == 88 && n.bytes[4] == 80 && !n.original && n.finish == 1 && n.cleanup == 1);
    assert(!memcmp(n.bytes, "RIFF", 4) && !memcmp(n.bytes + 12, "fmt ", 4) && !memcmp(n.bytes + 38, "fact", 4));
    total = n.step;
    for (fail = 1; fail <= total; ++fail)
    {
        memset(&n, 0, sizeof(n));
        n.fail = fail;
        number = 9;
        ok = three_mm_wave_native_import(NULL, NULL, 2, NULL, &number, &s);
        assert(!ok && n.cleanup == 1 && (number == 9 || (fail == total && number == 123)));
        assert(n.bad == (fail == 2 || fail == 3 || fail == 4 || fail == 5 || fail == 7 || fail == 8));
    }
}

typedef struct Physical
{
    int fail, step, bad, live;
    unsigned char input[12];
} Physical;
static int physical_step(Physical *p)
{
    return ++p->step != p->fail;
}
static ThreeMMI32 physical_size(void *p, void *source)
{
    (void)p;
    (void)source;
    return 12;
}
static int physical_read(void *p, void *source, void *data, ThreeMMI32 bytes)
{
    Physical *f = (Physical *)p;
    (void)source;
    assert(bytes == 12);
    memcpy(data, f->input, 12);
    return physical_step(f);
}
static int physical_prepare(void *p, const void *data, ThreeMMI32 bytes, const void **out, ThreeMMI32 *count)
{
    *out = data;
    *count = bytes;
    return physical_step((Physical *)p);
}
static const void *physical_name(void *p, void *source, const void *name)
{
    (void)source;
    return name ? name : p;
}
static void *physical_create(void *p)
{
    Physical *f = (Physical *)p;
    if (!physical_step(f))
        return NULL;
    ++f->live;
    return p;
}
static int physical_write(void *p, void *temp, const void *data, ThreeMMI32 bytes)
{
    assert(temp == p && data && bytes == 12);
    return physical_step((Physical *)p);
}
static int physical_resource(void *p, void *temp, void *dest, ThreeMMI32 style, const void *name, void *out)
{
    (void)dest;
    assert(temp == p && style == 3 && name == p);
    *(ThreeMMU32 *)out = 99;
    return physical_step((Physical *)p);
}
static void physical_release(void *p, void *temp)
{
    assert(p == temp);
    --((Physical *)p)->live;
}
static void physical_bad(void *p)
{
    ++((Physical *)p)->bad;
}
static void physical(void)
{
    Physical f;
    ThreeMMWaveFileServices s;
    ThreeMMStorageAllocator a;
    ThreeMMWaveBuffer b;
    ThreeMMWaveImportServices ws;
    ThreeMMU32 number, size;
    const unsigned char *selected;
    unsigned char wave[44] = {0};
    Fault fault;
    Import input;
    int fail, ok;
    memset(&s, 0, sizeof(s));
    s.context = &f;
    s.size = physical_size;
    s.read = physical_read;
    s.prepare = physical_prepare;
    s.name = physical_name;
    s.create_temp = physical_create;
    s.write_temp = physical_write;
    s.write_resource = physical_resource;
    s.release = physical_release;
    s.bad_sound = physical_bad;
    for (fail = 0; fail < 7; ++fail)
    {
        memset(&f, 0, sizeof(f));
        f.fail = fail;
        number = 7;
        ok = three_mm_wave_file_import(NULL, NULL, 3, NULL, &number, &s);
        assert(ok == (fail == 0 || fail == 6) && !f.live && f.bad == (fail == 1 || fail == 2));
        assert(number == (fail == 0 || fail >= 5 ? 99 : 7));
    }
    memset(&fault, 0, sizeof(fault));
    fault.fail = 1;
    a.context = &fault;
    a.resize = resize;
    a.release = release;
    memset(&f, 0, sizeof(f));
    s.allocator = &a;
    assert(!three_mm_wave_file_import(NULL, NULL, 3, NULL, &number, &s) && !fault.live && !f.step && !f.bad);
    /* C encoding failure releases converted host samples and preserves outputs. */
    memset(&input, 0, sizeof(input));
    memset(&fault, 0, sizeof(fault));
    fault.fail = 1;
    ws.context = &input;
    ws.load = load;
    ws.convert_u8 = convert;
    ws.release_samples = samples_release;
    assert(three_mm_wave_buffer_init(&b, &a));
    selected = wave;
    size = 77;
    assert(!three_mm_wave_prepare(wave, sizeof(wave), &ws, &b, &selected, &size));
    assert(input.releases == 1 && selected == wave && size == 77 && !fault.live);
    three_mm_wave_buffer_dispose(&b);
}
int main(void)
{
    midi();
    wave();
    factories();
    native();
    physical();
    puts("C89 sound payloads: parser/import, RIFF/ADPCM, factories and import failure cleanup passed.");
    return 0;
}
