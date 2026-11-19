#include "waveform_core.h"
#include <float.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "waveform test line %d: %s\n", __LINE__, #expression); return 0; \
} } while (0)

typedef struct Media
{
    int opens, measures, decodes, open_result, midi;
    size_t frames;
} Media;

static int open_media(void *p, MarcusWaveformMedia *media, char *message, size_t capacity)
{
    Media *source;
    source = (Media *)p;
    ++source->opens;
    media->source = -4;
    media->type = 5;
    media->number = 6;
    media->midi = source->midi;
    media->last_midi_ms = 125;
    if (!source->open_result && capacity >= sizeof("unavailable"))
        memcpy(message, "unavailable", sizeof("unavailable"));
    return source->open_result;
}

static int measure(void *p, double *seconds)
{
    Media *source;
    source = (Media *)p;
    ++source->measures;
    *seconds = (double)source->frames / 48000;
    return 1;
}

static int decode(void *p, const float **pcm, size_t *frames)
{
    static const float stereo[] = {-0.5f, 0.75f};
    Media *source;
    source = (Media *)p;
    ++source->decodes;
    *frames = source->frames;
    *pcm = stereo;
    return 1;
}

static int cache_keys(void)
{
    Media media = {0, 0, 0, 1, 0, 1};
    MarcusWaveformCacheState cache = {0};
    MarcusWaveformRequest key = {0, 1, 2, 3, 4, 0};
    MarcusWaveformServices services = {NULL, open_media, measure, decode};
    MarcusWaveformInfo info;
    const MarcusWaveform *wave;
    MarcusWaveformColumn peak;
    int i;

    services.context = &media;
    CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1);
    CHECK(info.kind == MARCUS_WAVEFORM_SAMPLED && info.seconds == 1.0 / 48000);
    CHECK(info.resolved_source == -4 && info.resolved_type == 5 && info.resolved_chunk == 6);
    CHECK(media.opens == 1 && media.measures == 1 && !media.decodes);
    CHECK(marcus_waveform_cache_peaks(&cache, &key, &services, &wave, &info) == 1);
    marcus_waveform_read(wave, 0, 1, 1, &peak, 0);
    CHECK(peak.minimum == -.5f && peak.maximum == .75f && media.opens == 2 && media.decodes == 1);

    ++key.scene;
    media.midi = 1;
    CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1);
    CHECK(info.kind == MARCUS_WAVEFORM_MIDI && info.seconds == .125 && !info.message[0]);
    CHECK(marcus_waveform_cache_peaks(&cache, &key, &services, &wave, &info) == 1);
    CHECK(info.kind == MARCUS_WAVEFORM_MIDI && info.seconds == .125 && info.message[0] && media.opens == 4);

    key.source = -1;
    CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1);
    ++key.scene;
    CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1 && media.opens == 5);
    key.silent = 1;
    CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1);
    CHECK(info.kind == MARCUS_WAVEFORM_SILENT && !info.message[0]);
    CHECK(marcus_waveform_cache_peaks(&cache, &key, &services, &wave, &info) == 1);
    CHECK(info.kind == MARCUS_WAVEFORM_SILENT && info.message[0] && !wave && media.opens == 5);
    key.silent = 0;
    ++key.child;
    media.open_result = -1;
    CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == -1);
    media.open_result = 0;
    CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1);
    CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1 && media.opens == 7);
    CHECK(info.kind == MARCUS_WAVEFORM_UNAVAILABLE && !strcmp(info.message, "unavailable"));

    media.open_result = 1;
    for (i = 100; i < 700; ++i)
    {
        key.number = (MMU32)i;
        CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1);
    }
    CHECK(media.opens == 607);
    for (i = 699; i >= 100; --i)
    {
        key.number = (MMU32)i;
        CHECK(marcus_waveform_cache_length(&cache, &key, &services, &info) == 1 && info.seconds == .125);
    }
    CHECK(media.opens == 607);
    marcus_waveform_cache_clear(&cache);
    return 1;
}

static int peak_limits(void)
{
    Media media = {0, 0, 0, 1, 1, 1};
    MarcusWaveformCacheState cache = {0};
    MarcusWaveformRequest key = {0, 0, 0, 0, 0, 0};
    MarcusWaveformServices services = {NULL, open_media, measure, decode};
    MarcusWaveformInfo info;
    const MarcusWaveform *wave;
    unsigned i;
    services.context = &media;
    for (i = 0; i < 512; ++i)
    {
        key.number = i;
        CHECK(marcus_waveform_cache_peaks(&cache, &key, &services, &wave, &info) == 1);
    }
    key.number = 0;
    CHECK(marcus_waveform_cache_peaks(&cache, &key, &services, &wave, &info) == 1 && media.opens == 512);
    key.number = 512;
    media.midi = 0;
    media.frames = ((size_t)256 * 1024 * 1024) / (2 * sizeof(float)) + 1;
    CHECK(marcus_waveform_cache_peaks(&cache, &key, &services, &wave, &info) == 1);
    CHECK(info.kind == MARCUS_WAVEFORM_UNAVAILABLE && strstr(info.message, "budget"));
    key.number = 0;
    CHECK(marcus_waveform_cache_peaks(&cache, &key, &services, &wave, &info) == 1 && media.opens == 513);
    key.number = 1;
    media.midi = 1;
    CHECK(marcus_waveform_cache_peaks(&cache, &key, &services, &wave, &info) == 1 && media.opens == 514);
    marcus_waveform_cache_clear(&cache);
    return 1;
}

static double time_at(const void *context, double frame)
{
    return (frame + *(const double *)context) / 48000;
}

static int ranges(void)
{
    float stereo[256 * 2] = {0};
    const double origin = 3;
    MarcusWaveform waveform = {0};
    MarcusWaveformColumn peaks[4];
    stereo[0] = .5f;
    stereo[sizeof(stereo) / sizeof(float) - 1] = -.75f;
    CHECK(marcus_waveform_build(&waveform, stereo, sizeof(stereo) / (2 * sizeof(float))));
    marcus_waveform_read(&waveform, 255.0 / 48000, 2.0 / 48000, 1, peaks, 1);
    CHECK(peaks[0].minimum == -.75f && peaks[0].maximum == .5f);
    marcus_waveform_read(&waveform, DBL_MAX, DBL_MAX, 4, peaks, 1);
    CHECK(peaks[3].minimum == -.75f && peaks[3].maximum == .5f);
    marcus_waveform_read_timeline(&waveform, 0, 256.0 / 48000, -131, 128, 4, peaks, 0, time_at, &origin);
    CHECK(!peaks[0].minimum && !peaks[0].maximum && peaks[1].maximum == .5f && peaks[2].minimum == -.75f);
    CHECK(!peaks[3].minimum && !peaks[3].maximum);
    marcus_waveform_dispose(&waveform);
    return 1;
}

int main(void)
{
    return cache_keys() && peak_limits() && ranges() ? 0 : 1;
}
