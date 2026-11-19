#include "mm/preview_media.h"
#include "mm/text.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Caption
{
    MMPreviewCaptionServices services;
    MMStorageAllocator allocator;
    MMPreviewCaptionData data;
    MMPreviewTextProperty properties[64];
    MMU32 raw[64][2], fonts[64], units, converted, text_size;
    unsigned short text[128];
    unsigned char argument[16];
    int calls, fail_at, failure, owner, released, published, font, appended, argument_bytes, free_argument;
    int allocations, live, fail_alloc;
} Caption;
static int step(Caption *h)
{
    return ++h->calls == h->fail_at ? h->failure : 1;
}
static void *resize(void *v, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Caption *h = (Caption *)v;
    void *p;
    (void)old_bytes;
    (void)priority;
    ++h->allocations;
    if (h->fail_alloc && h->allocations >= h->fail_alloc)
        return 0;
    p = realloc(old, bytes);
    if (p && !old)
        ++h->live;
    return p;
}
static void free_buffer(void *v, void *p, MMU32 bytes)
{
    Caption *h = (Caption *)v;
    (void)bytes;
    assert(h->live > 0);
    --h->live;
    free(p);
}
static int create(void *v, void **out)
{
    Caption *h = (Caption *)v;
    int r = step(h);
    if (r >= 0)
    {
        *out = h;
        h->owner = 1;
    }
    return r;
}
static int box(void *v, void *owner, MMU32 number, MMPreviewCaptionData *out)
{
    Caption *h = (Caption *)v;
    int r = step(h);
    assert(owner == h && number == 9);
    if (r > 0)
        *out = h->data;
    return r;
}
static int rich(void *v, void *owner, MMPreviewCaptionData *out)
{
    Caption *h = (Caption *)v;
    (void)out;
    assert(owner == h);
    return step(h);
}
static void release(void *v, void *owner)
{
    Caption *h = (Caption *)v;
    assert(owner == h && h->owner && !h->live);
    h->owner = 0;
    ++h->released;
}
static void publish(void *v, const MMPreviewCaptionData *p)
{
    Caption *h = (Caption *)v;
    assert(p->bytes == h->data.bytes);
    ++h->published;
}
static int font(void *v, void *owner)
{
    Caption *h = (Caption *)v;
    assert(owner == h);
    h->font = 1;
    return step(h);
}
static int read_text(void *v, void *owner, void *out, MMU32 n)
{
    Caption *h = (Caption *)v;
    int r = step(h);
    assert(owner == h);
    memset(out, 'A', n);
    return r;
}
static int utf16(void *v, const void *p, MMU32 n, int encoding, unsigned short *out, MMU32 capacity, MMU32 *count)
{
    Caption *h = (Caption *)v;
    MMU32 i;
    int r = step(h);
    (void)p;
    (void)n;
    (void)encoding;
    if (r > 0)
    {
        *count = out ? h->converted : h->units;
        if (out)
            for (i = 0; i < *count && i < capacity; ++i)
                out[i] = (unsigned short)('A' + i);
    }
    return r;
}
static int text_buffer(void *v, MMU32 n, unsigned short **out)
{
    Caption *h = (Caption *)v;
    int r = step(h);
    assert(n <= 128);
    if (r > 0)
    {
        memset(h->text, 0, sizeof(h->text));
        h->text_size = n;
        *out = h->text;
    }
    return r;
}
static void property(void *v, void *owner, MMU32 i, MMU32 *out)
{
    Caption *h = (Caption *)v;
    assert(owner == h && i < h->data.properties);
    memcpy(out, h->raw[i], 8);
}
static void argument_info(void *v, void *owner, MMU32 i, MMI32 *bytes, int *free_entry)
{
    Caption *h = (Caption *)v;
    assert(owner == h && i == 0);
    *bytes = h->argument_bytes;
    *free_entry = h->free_argument;
}
static void argument(void *v, void *owner, MMU32 i, void *out)
{
    Caption *h = (Caption *)v;
    assert(owner == h && i == 0 && h->argument_bytes <= 16);
    memcpy(out, h->argument, (size_t)h->argument_bytes);
}
static int append(void *v, const MMPreviewTextProperty *p, const void *font_data, MMU32 font_bytes)
{
    Caption *h = (Caption *)v;
    int r = step(h);
    if (r > 0)
    {
        assert(h->appended < 64);
        h->properties[h->appended] = *p;
        if (font_data)
            assert(font_bytes == 2 && !memcmp(font_data, h->argument + 6, 2));
        h->fonts[h->appended++] = font_data ? 1U : 0U;
    }
    return r;
}
static MMU32 position(void *v, MMU32 i)
{
    return ((Caption *)v)->properties[i].position;
}
static void swap(void *v, MMU32 a, MMU32 b)
{
    Caption *h = (Caption *)v;
    MMPreviewTextProperty p = h->properties[a];
    MMU32 f = h->fonts[a];
    h->properties[a] = h->properties[b];
    h->properties[b] = p;
    h->fonts[a] = h->fonts[b];
    h->fonts[b] = f;
}
static void setup(Caption *h)
{
    MMPreviewCaptionServices *s = &h->services;
    unsigned i;
    const MMU32 positions[] = {2, 0, 2, 1, 0};
    memset(h, 0, sizeof(*h));
    h->data.bytes = 4;
    h->data.encoding = MM_TEXT_WINDOWS;
    h->data.properties = 5;
    h->data.has_arguments = 1;
    h->data.arguments = 1;
    h->units = h->converted = 4;
    h->argument_bytes = 8;
    h->argument[6] = 0xaa;
    h->argument[7] = 0xbb;
    for (i = 0; i < 5; ++i)
    {
        h->raw[i][0] = 0x01000000 | positions[i];
        h->raw[i][1] = i;
    }
    h->raw[2][0] = 0x02000002;
    h->raw[2][1] = 1;
    h->allocator.context = h;
    h->allocator.resize = resize;
    h->allocator.release = free_buffer;
    s->context = h;
    s->input = h;
    s->allocator = &h->allocator;
    s->create = create;
    s->box = box;
    s->rich_text = rich;
    s->release = release;
    s->publish = publish;
    s->font = font;
    s->read_text = read_text;
    s->utf16 = utf16;
    s->text_buffer = text_buffer;
    s->property = property;
    s->argument_info = argument_info;
    s->argument = argument;
    s->append = append;
    s->position = position;
    s->swap = swap;
}
static MMU32 swapped(MMU32 x)
{
    return (x >> 24) | ((x >> 8) & 0xff00U) | ((x << 8) & 0xff0000U) | (x << 24);
}
static void caption_tests(void)
{
    Caption h;
    MMPreviewTextProperty expected[64];
    int i, n, f, r;
    unsigned j;
    setup(&h);
    assert(mm_preview_caption(9, &h.services) == 1 && h.released == 1 && !h.live && h.appended == 5);
    assert(h.properties[0].value == 1 && h.properties[1].value == 4 && h.properties[2].value == 3 &&
           h.properties[3].value == 0 && h.properties[4].kind == 2 && h.fonts[4]);
    n = h.calls;
    for (f = -2; f <= 0; ++f)
        for (i = 1; i <= n; ++i)
        {
            setup(&h);
            h.fail_at = i;
            h.failure = f;
            r = mm_preview_caption(9, &h.services);
            assert(!h.owner && !h.live && r == (f == 0 && i == 1 ? -1 : f));
        }
    setup(&h);
    h.data.bytes = 1024 * 1024 + 1;
    assert(!mm_preview_caption(9, &h.services) && !h.published && !h.allocations && h.released == 1);
    setup(&h);
    h.units = 0;
    assert(!mm_preview_caption(9, &h.services) && h.published && h.font && !h.text_size);
    setup(&h);
    h.data.bytes = 0;
    h.units = 0;
    h.data.properties = 0;
    assert(mm_preview_caption(9, &h.services) == 1 && !h.text_size && !h.allocations);
    setup(&h);
    h.converted = 2;
    assert(mm_preview_caption(9, &h.services) == 1 && h.text_size == 4 && h.text[2] == 0);
    setup(&h);
    h.raw[3][0] = 0x01000005;
    assert(!mm_preview_caption(9, &h.services) && h.appended == 3 && h.properties[0].position == 2 && !h.live);
    setup(&h);
    h.data.has_arguments = 0;
    assert(!mm_preview_caption(9, &h.services) && h.appended == 2);
    setup(&h);
    h.free_argument = 1;
    assert(!mm_preview_caption(9, &h.services));
    setup(&h);
    h.argument_bytes = 5;
    assert(!mm_preview_caption(9, &h.services));
    setup(&h);
    h.raw[2][1] = 0xffffffffU;
    assert(mm_preview_caption(9, &h.services) == 1 && !h.fonts[4] && h.properties[4].value == 0xffffffffU);
    setup(&h);
    h.data.property_swap = 1;
    for (j = 0; j < 5; ++j)
    {
        h.raw[j][0] = swapped(h.raw[j][0]);
        h.raw[j][1] = swapped(h.raw[j][1]);
    }
    assert(mm_preview_caption(9, &h.services) == 1 && h.fonts[4]);
    for (i = 1; i <= 5; ++i)
    {
        setup(&h);
        h.fail_alloc = i;
        r = mm_preview_caption(9, &h.services);
        assert(!h.live && !h.owner);
        assert(r == (i <= 2 ? -1 : 1));
    }
    for (n = 0; n <= 64; ++n)
        for (f = 0; f < 3; ++f)
        {
            setup(&h);
            h.data.bytes = 64;
            h.units = h.converted = 64;
            h.data.properties = (MMU32)n;
            h.fail_alloc = f == 1 ? 2 : f == 2 ? 3 : 0;
            for (i = 0; i < n; ++i)
            {
                h.raw[i][0] = 0x01000000 | ((MMU32)i * 37 % 17);
                h.raw[i][1] = (MMU32)i;
            }
            assert(mm_preview_caption(9, &h.services) == 1 && !h.live);
            for (i = 1; i < n; ++i)
                assert(h.properties[i - 1].position < h.properties[i].position ||
                       (h.properties[i - 1].position == h.properties[i].position &&
                        h.properties[i - 1].value < h.properties[i].value));
            if (!f)
                memcpy(expected, h.properties, sizeof(expected));
            else
                assert(!memcmp(expected, h.properties, sizeof(expected)));
        }
}
typedef struct Audio
{
    MMPreviewAudioServices services;
    MMMovieSessionGraph graph;
    MMStorageAllocator allocator;
    unsigned char header[16], bytes[64], wave[64], *large;
    MMU32 length, header_length, payload_type, type, number, emitted, wave_size, time[16], message[16];
    int calls, fail_at, failure, expected, silent, midi, has_child, bad_size, fail_read;
    MMU32 parent_number, reported_size;
    int allocations, fail_alloc, live;
} Audio;
static int au_step(Audio *h)
{
    return ++h->calls == h->fail_at ? h->failure : 1;
}
static void *au_resize(void *v, void *p, MMU32 old, MMU32 bytes, int priority)
{
    Audio *h = (Audio *)v;
    void *out;
    (void)old;
    (void)priority;
    ++h->allocations;
    if (h->fail_alloc && h->allocations >= h->fail_alloc)
        return 0;
    out = realloc(p, bytes);
    if (out && !p)
        ++h->live;
    return out;
}
static void au_free(void *v, void *p, MMU32 bytes)
{
    Audio *h = (Audio *)v;
    (void)bytes;
    assert(h->live > 0);
    --h->live;
    free(p);
}
static void reset(void *v)
{
    Audio *h = (Audio *)v;
    h->silent = h->midi = 0;
    h->type = h->number = h->emitted = h->wave_size = 0;
    memset(h->wave, 0, 64);
}
static void silent(void *v)
{
    ((Audio *)v)->silent = 1;
}
static int audio_entry(void *v, MMU32 position, MMChunkEntry *out)
{
    const Audio *h = (const Audio *)v;
    int parent = position == (h->payload_type == MM_SOUND_MIDI ? 1U : 0U);
    assert(position < 2);
    memset(out, 0, sizeof(*out));
    out->type = parent ? MM_SOUND_TYPE : h->payload_type;
    out->number = parent ? h->parent_number : 10;
    out->bytes = parent ? h->header_length : h->bad_size ? h->reported_size : h->length;
    out->child_count = parent && h->has_child;
    return 1;
}
static int audio_child(void *v, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    const Audio *h = (const Audio *)v;
    assert(parent == (h->payload_type == MM_SOUND_MIDI ? 1U : 0U) && !position && h->has_child);
    out->slot = 0;
    out->type = h->payload_type;
    out->number = 10;
    return 1;
}
static void audio_access(void *v, MMChunkAccess *out)
{
    MMChunkAccess access = {NULL, 2, audio_entry, audio_child};
    access.context = v;
    *out = access;
}
static int fail_audio_read(void *v, MMU32 offset, void *out, size_t count)
{
    (void)v;
    assert(!offset);
    memset(out, 0x5a, count / 2);
    return 0;
}
static int audio_payload(void *v, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    Audio *h = (Audio *)v;
    MMMemoryStream memory;
    const unsigned char *bytes = entry->type == MM_SOUND_TYPE ? h->header : h->large ? h->large : h->bytes;
    int r = au_step(h);
    if (r <= 0)
    {
        h->expected = r == -1 ? -1 : 0;
        return r == -1 ? MM_MOVIE_MEMORY : MM_MOVIE_IO;
    }
    assert(mm_memory_stream_init(&memory, bytes, entry->bytes));
    if (h->fail_read)
        memory.stream.read_at = fail_audio_read;
    return receive(out, &memory.stream);
}
static void audio_publish(void *v, MMU32 type, MMU32 number, int midi)
{
    Audio *h = (Audio *)v;
    h->type = type;
    h->number = number;
    h->midi = midi;
}
static int wave_buffer(void *v, MMU32 count, unsigned char **out)
{
    Audio *h = (Audio *)v;
    int r = au_step(h);
    assert(count <= 64);
    if (r <= 0)
        h->expected = r;
    if (r > 0)
    {
        *out = h->wave;
        h->wave_size = count;
    }
    return r;
}
static int midi_event(void *v, MMU32 timestamp, MMU32 message)
{
    Audio *h = (Audio *)v;
    int r = au_step(h);
    if (r <= 0)
        h->expected = r;
    if (r > 0)
    {
        if (h->emitted < 16)
        {
            h->time[h->emitted] = timestamp;
            h->message[h->emitted] = message;
        }
        ++h->emitted;
    }
    return r;
}
static void au_setup(Audio *h, int midi)
{
    MMPreviewAudioServices *s = &h->services;
    const MMMovieSessionGraph graph = {NULL, audio_access, NULL, audio_payload, NULL};
    const unsigned char notes[] = {0, 0x90, 60,   100,  5, 61,   90, 0,    0xff, 0x51,
                                   3, 7,    0xa1, 0x20, 0, 0xf8, 5,  0x80, 60,   0};
    memset(h, 0, sizeof(*h));
    h->graph = graph;
    h->graph.context = h;
    h->parent_number = 9;
    h->header[0] = 1;
    h->header_length = 16;
    h->has_child = 1;
    h->payload_type = midi ? MM_SOUND_MIDI : MM_SOUND_WAVE;
    memcpy(h->bytes, notes, sizeof(notes));
    h->length = sizeof(notes);
    h->allocator.context = h;
    h->allocator.resize = au_resize;
    h->allocator.release = au_free;
    s->context = h;
    s->allocator = &h->allocator;
    s->reset = reset;
    s->silent = silent;
    s->publish = audio_publish;
    s->wave_buffer = wave_buffer;
    s->event = midi_event;
}
static void audio_tests(void)
{
    Audio h;
    int mode, n, i, f, r;
    MMU32 at;
    unsigned char delta[5];
    for (mode = 0; mode < 2; ++mode)
    {
        au_setup(&h, mode);
        assert(mm_preview_audio(&h.graph, MM_SOUND_TYPE, 9, &h.services) == 1 && !h.live && h.type == h.payload_type &&
               h.number == 10);
        assert(mode ? (h.emitted == 3 && h.time[1] == 5 && h.time[2] == 10)
                    : (h.wave_size == h.length && !memcmp(h.wave, h.bytes, h.length)));
        n = h.calls;
        for (f = -2; f <= 0; ++f)
            for (i = 1; i <= n; ++i)
            {
                au_setup(&h, mode);
                h.fail_at = i;
                h.failure = f;
                r = mm_preview_audio(&h.graph, MM_SOUND_TYPE, 9, &h.services);
                assert(h.calls == i && r == h.expected && !h.live);
            }
    }
    au_setup(&h, 0);
    h.header[0] = 0;
    h.header[1] = 1;
    h.header[15] = 1;
    assert(mm_preview_audio(&h.graph, MM_SOUND_TYPE, 9, &h.services) == 1 && h.silent && !h.type);
    au_setup(&h, 0);
    h.has_child = 0;
    assert(mm_preview_audio(&h.graph, MM_SOUND_TYPE, 9, &h.services) == 1 && h.silent);
    au_setup(&h, 0);
    h.header_length = 15;
    assert(!mm_preview_audio(&h.graph, MM_SOUND_TYPE, 9, &h.services) && !h.type);
    au_setup(&h, 0);
    h.header[0] = 2;
    assert(!mm_preview_audio(&h.graph, MM_SOUND_TYPE, 9, &h.services));
    au_setup(&h, 0);
    h.bad_size = 1;
    h.reported_size = 64 * 1024 * 1024 + 1;
    assert(!mm_preview_audio(&h.graph, MM_SOUND_WAVE, 10, &h.services) && !h.type);
    au_setup(&h, 0);
    h.fail_read = 1;
    assert(!mm_preview_audio(&h.graph, MM_SOUND_WAVE, 10, &h.services) && h.type == MM_SOUND_WAVE &&
           h.wave_size == h.length && h.wave[0] == 0x5a);
    au_setup(&h, 1);
    h.fail_read = 1;
    assert(!mm_preview_audio(&h.graph, MM_SOUND_MIDI, 10, &h.services) && h.midi && !h.emitted && !h.live);
    au_setup(&h, 1);
    h.length = 0;
    assert(mm_preview_audio(&h.graph, MM_SOUND_MIDI, 10, &h.services) == 1 && h.midi && !h.emitted);
    au_setup(&h, 1);
    h.length = 5;
    h.bytes[4] = 0x81;
    assert(mm_preview_audio(&h.graph, MM_SOUND_MIDI, 10, &h.services) == 1 && h.emitted == 1);
    au_setup(&h, 1);
    h.fail_alloc = 1;
    assert(mm_preview_audio(&h.graph, MM_SOUND_MIDI, 10, &h.services) == 0 && h.midi && h.type == MM_SOUND_MIDI && !h.live);
    au_setup(&h, 1);
    at = mm_midi_encode_time(86400000, delta);
    memcpy(h.bytes, delta, at);
    memcpy(h.bytes + at, "\x90\x3c\x64\x01\xff\x51\x03\x07\xa1\x20", 10);
    h.length = at + 10;
    assert(!mm_preview_audio(&h.graph, MM_SOUND_MIDI, 10, &h.services) && h.emitted == 1 && h.time[0] == 86400000);
    au_setup(&h, 1);
    h.length = 4000007;
    h.large = (unsigned char *)malloc(h.length);
    assert(h.large);
    for (at = 0; at < 4000000; at += 4)
    {
        h.large[at] = 0;
        h.large[at + 1] = 0x90;
        h.large[at + 2] = 60;
        h.large[at + 3] = 100;
    }
    memcpy(h.large + 4000000, "\x00\xff\x51\x03\x07\xa1\x20", 7);
    assert(!mm_preview_audio(&h.graph, MM_SOUND_MIDI, 10, &h.services) && h.emitted == 1000000 && !h.live);
    free(h.large);
}
typedef struct Sound
{
    Audio audio;
    MMChunkAccess movie;
    MMPreviewSoundServices services;
    int scene_found, movie_found, first_movie_found, missing, resolve_error;
    int source_calls, resolve_calls, graph_calls;
    MMI32 source;
    MMU32 resolved;
} Sound;

static int sound_entry(void *v, MMU32 position, MMChunkEntry *out)
{
    Sound *h = (Sound *)v;
    const MMU32 numbers[] = {6, 7, 3, 9};
    assert(position < 4);
    ++h->graph_calls;
    memset(out, 0, sizeof(*out));
    out->type = position < 2 ? 0x4d564945U : 0x5343454eU;
    out->number = numbers[position];
    out->child_count = position == 0 ? h->first_movie_found : position == 1 ? h->movie_found :
        position == 3 && h->scene_found;
    return 1;
}
static int sound_child(void *v, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    const MMU32 targets[] = {26, 27, 0, 29};
    (void)v;
    assert(parent < 4 && !position);
    out->type = MM_SOUND_TYPE;
    out->slot = 4;
    out->number = targets[parent];
    return 1;
}
static void so_source(void *v, MMI32 source)
{
    Sound *h = (Sound *)v;
    assert(source == h->source);
    ++h->source_calls;
}
static int resolve_sound(void *v, MMI32 source, MMU32 type, MMU32 number, MMMovieSessionGraph *graph)
{
    Sound *h = (Sound *)v;
    assert(type == MM_SOUND_TYPE);
    ++h->resolve_calls;
    h->source = source;
    h->resolved = number;
    h->audio.parent_number = number;
    *graph = h->audio.graph;
    return h->resolve_error ? h->resolve_error : !h->missing;
}
static void so_setup(Sound *h)
{
    const MMChunkAccess movie = {NULL, 4, sound_entry, sound_child};
    const MMPreviewSoundServices services = {NULL, so_source, resolve_sound};
    memset(h, 0, sizeof(*h));
    au_setup(&h->audio, 1);
    h->movie = movie;
    h->movie.context = h;
    h->services = services;
    h->services.context = h;
    h->movie_found = 1;
}
static int sound(Sound *h, const MMPreviewCue *cue, int ready, MMU32 *resolved)
{
    return mm_preview_sound(cue, ready, 3, &h->movie, resolved, &h->services, &h->audio.services);
}
static void sound_tests(void)
{
    Sound h;
    MMPreviewCue cue = {0};
    MMU32 resolved;
    int f;
    cue.type = MM_SOUND_TYPE;
    cue.number = 77;
    cue.child = 4;
    so_setup(&h);
    assert(sound(&h, &cue, 0, &resolved) == MM_PREVIEW_SOUND_NOT_READY && !h.graph_calls && !h.resolve_calls);
    cue.silent = 1;
    assert(sound(&h, &cue, 0, &resolved) == 1 && h.audio.silent && !h.resolve_calls && !h.source_calls);
    cue.silent = 0;
    so_setup(&h);
    assert(sound(&h, &cue, 1, &resolved) == 1 && resolved == 27 && h.resolved == 27 && h.source_calls == 1 &&
           h.audio.emitted == 3);
    so_setup(&h);
    h.first_movie_found = 1;
    assert(sound(&h, &cue, 1, &resolved) == 1 && resolved == 26 && h.source_calls == 1);
    for (f = -2; f < 0; ++f)
    {
        so_setup(&h);
        h.resolve_error = f;
        assert(sound(&h, &cue, 1, &resolved) == f && !h.source_calls && !h.audio.calls);
    }
    so_setup(&h);
    h.scene_found = 1;
    cue.scene_type = 0x5343454eU;
    cue.scene_number = 9;
    assert(sound(&h, &cue, 1, &resolved) == 1 && resolved == 29);
    so_setup(&h);
    h.movie_found = 0;
    assert(sound(&h, &cue, 1, &resolved) == 1 && resolved == 77);
    so_setup(&h);
    h.missing = 1;
    assert(sound(&h, &cue, 1, &resolved) == MM_PREVIEW_SOUND_MISSING && !h.audio.calls && !h.source_calls);
    so_setup(&h);
    h.audio.fail_read = 1;
    assert(!sound(&h, &cue, 1, &resolved) && !h.source_calls);
    so_setup(&h);
    cue.source = 2;
    assert(sound(&h, &cue, 1, &resolved) == 1 && !h.graph_calls && resolved == 77 && h.source == 2);
}
int main(void)
{
    caption_tests();
    audio_tests();
    sound_tests();
    puts("Preview media: caption prefixes/UTF-16/properties, stable sorting, envelope/MIDI budgets, source remapping "
         "and fault cleanup passed.");
    return 0;
}
