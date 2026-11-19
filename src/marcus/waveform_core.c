#include "waveform_core.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { bucket_frames = 128, sample_rate = 48000, peak_limit = 512 };
#define PEAK_BUDGET ((size_t)32 * 1024 * 1024)
#define DECODE_BUDGET ((size_t)256 * 1024 * 1024)

static void merge(MarcusWaveformColumn *peak, const MarcusWaveformColumn *other)
{
    if (other->minimum < peak->minimum)
        peak->minimum = other->minimum;
    if (other->maximum > peak->maximum)
        peak->maximum = other->maximum;
}

int marcus_waveform_build(MarcusWaveform *wave, const float *stereo, size_t frames)
{
    size_t buckets, leaves, i, channel;
    MarcusWaveformColumn *tree, *peak;
    float value;
    if (frames > (size_t)-1 / (2 * sizeof(float)) || (frames && !stereo))
        return 0;
    buckets = frames / bucket_frames + (frames % bucket_frames != 0);
    leaves = 1;
    while (leaves < buckets)
        leaves *= 2;
    tree = wave->tree;
    if (leaves != wave->leaves)
    {
        tree = (MarcusWaveformColumn *)malloc(leaves * 2 * sizeof(*tree));
        if (!tree)
            return 0;
        free(wave->tree);
    }
    for (i = 0; i < leaves * 2; ++i)
        tree[i].minimum = tree[i].maximum = 0.f;
    for (i = 0; i < frames; ++i)
    {
        peak = &tree[leaves + i / bucket_frames];
        for (channel = 0; channel < 2; ++channel)
        {
            value = stereo[i * 2 + channel];
            if (value >= -FLT_MAX && value <= FLT_MAX)
            {
                if (value < peak->minimum)
                    peak->minimum = value;
                if (value > peak->maximum)
                    peak->maximum = value;
            }
        }
    }
    for (i = leaves; --i;)
    {
        tree[i] = tree[i * 2];
        merge(&tree[i], &tree[i * 2 + 1]);
    }
    wave->tree = tree;
    wave->frames = frames;
    wave->leaves = leaves;
    return 1;
}

void marcus_waveform_dispose(MarcusWaveform *wave)
{
    free(wave->tree);
    wave->tree = NULL;
    wave->frames = wave->leaves = 0;
}

double marcus_waveform_seconds(const MarcusWaveform *wave)
{
    return wave ? (double)wave->frames / sample_rate : 0;
}

static void range(const MarcusWaveform *wave, double start, double end, MarcusWaveformColumn *peak)
{
    size_t left, right;
    left = wave->leaves + (size_t)floor(start * sample_rate / bucket_frames);
    right = wave->leaves + (size_t)ceil(end * sample_rate / bucket_frames);
    if (right > wave->leaves * 2)
        right = wave->leaves * 2;
    while (left < right)
    {
        if (left & 1)
            merge(peak, &wave->tree[left++]);
        if (right & 1)
            merge(peak, &wave->tree[--right]);
        left /= 2;
        right /= 2;
    }
}

static int valid_window(double first, double step)
{
    return first >= 0 && first <= DBL_MAX && step > 0 && step <= DBL_MAX;
}

static void column_at(const MarcusWaveform *wave, double duration, double start, double step, int repeat,
                      MarcusWaveformColumn *peak)
{
    double end;
    if (repeat)
    {
        if (step >= duration)
        {
            *peak = wave->tree[1];
            return;
        }
        start = fmod(start, duration);
    }
    if (!(start >= 0 && start < duration))
        return;
    end = start + step;
    range(wave, start, end < duration ? end : duration, peak);
    if (repeat && end > duration)
        range(wave, 0, end - duration, peak);
}

static void silence(MarcusWaveformColumn *columns, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        columns[i].minimum = columns[i].maximum = 0.f;
}

void marcus_waveform_read(const MarcusWaveform *wave, double first, double step, int count,
                          MarcusWaveformColumn *columns, int repeat)
{
    double duration, start;
    int i;
    silence(columns, count);
    duration = marcus_waveform_seconds(wave);
    if (!duration || !valid_window(first, step))
        return;
    for (i = 0; i < count; ++i)
    {
        start = first + i * step;
        if (!repeat && start >= duration)
            break;
        column_at(wave, duration, start, step, repeat, &columns[i]);
    }
}

void marcus_waveform_read_timeline(const MarcusWaveform *wave, double start, double end, double first, double step,
                                   int count, MarcusWaveformColumn *columns, int repeat,
                                   double (*time_at)(const void *, double), const void *context)
{
    double duration, entry, finish;
    int i;
    silence(columns, count);
    duration = marcus_waveform_seconds(wave);
    if (!duration)
        return;
    for (i = 0; i < count; ++i)
    {
        entry = time_at(context, first + i * step);
        finish = time_at(context, first + (i + 1) * step);
        if (!(finish < end))
            finish = end;
        if (entry >= start && entry < end && finish > entry && valid_window(entry - start, finish - entry))
            column_at(wave, duration, entry - start, finish - entry, repeat, &columns[i]);
    }
}

typedef struct WaveKey
{
    MMI32 source;
    MMU32 type, number, child, scene;
} WaveKey;

typedef struct MarcusWaveformLength
{
    WaveKey key;
    MarcusWaveformInfo info;
} WaveLength;

typedef struct MarcusWaveformPeak
{
    struct MarcusWaveformPeak *next;
    WaveKey key;
    MarcusWaveformInfo info;
    MarcusWaveform envelope;
} WavePeak;

static WaveKey request_key(const MarcusWaveformRequest *request)
{
    WaveKey key;
    key.source = request->source;
    key.type = request->type;
    key.number = request->number;
    key.child = request->child;
    key.scene = key.source == 0 ? request->scene : 0;
    return key;
}

static int same_key(const WaveKey *a, const WaveKey *b)
{
    return a->source == b->source && a->type == b->type && a->number == b->number &&
           a->child == b->child && a->scene == b->scene;
}

static size_t length_index(WaveLength **table, size_t capacity, const WaveKey *key)
{
    MMU32 hash;
    size_t i, mask;
    hash = ((MMU32)key->source ^ key->type) * 16777619U;
    hash = (hash ^ key->number) * 16777619U;
    hash = (hash ^ key->child) * 16777619U;
    hash = (hash ^ key->scene) * 16777619U;
    mask = capacity - 1;
    i = (hash ^ (hash >> 16)) & mask;
    while (table[i] && !same_key(&table[i]->key, key))
        i = (i + 1) & mask;
    return i;
}

static int grow_lengths(MarcusWaveformCacheState *cache)
{
    WaveLength **grown, *entry;
    size_t capacity, i;
    if (cache->length_capacity > (size_t)-1 / (2 * sizeof(*grown)))
        return 0;
    capacity = cache->length_capacity ? cache->length_capacity * 2 : 32;
    grown = (WaveLength **)calloc(capacity, sizeof(*grown));
    if (!grown)
        return 0;
    for (i = 0; i < cache->length_capacity; ++i)
    {
        entry = cache->lengths[i];
        if (entry)
            grown[length_index(grown, capacity, &entry->key)] = entry;
    }
    free(cache->lengths);
    cache->lengths = grown;
    cache->length_capacity = capacity;
    return 1;
}

static void silent_info(MarcusWaveformInfo *info, int show_message)
{
    static const char stop[] = "Stop / silence";
    memset(info, 0, sizeof(*info));
    info->kind = MARCUS_WAVEFORM_SILENT;
    if (show_message)
        memcpy(info->message, stop, sizeof(stop));
}

static int load(const MarcusWaveformServices *services, MarcusWaveform *wave, MarcusWaveformInfo *info)
{
    static const char midi[] = "MIDI notes (no sampled waveform)";
    static const char decode_error[] = "Cannot decode sound waveform.";
    static const char over_budget[] = "Sound exceeds the waveform decode budget.";
    MarcusWaveformMedia media = {0};
    const float *stereo;
    size_t frames;
    int status;
    memset(info, 0, sizeof(*info));
    info->kind = MARCUS_WAVEFORM_UNAVAILABLE;
    status = services->open(services->context, &media, info->message, sizeof(info->message));
    if (status < 0)
        return status;
    if (status && media.silent)
        silent_info(info, wave != NULL);
    else if (status && media.midi)
    {
        info->kind = MARCUS_WAVEFORM_MIDI;
        info->seconds = media.last_midi_ms / 1000.0;
        if (wave)
            memcpy(info->message, midi, sizeof(midi));
    }
    else if (status && wave)
    {
        status = services->decode(services->context, &stereo, &frames);
        if (status < 0)
            return status;
        if (!status)
            memcpy(info->message, decode_error, sizeof(decode_error));
        else if (frames > DECODE_BUDGET / (2 * sizeof(float)))
            memcpy(info->message, over_budget, sizeof(over_budget));
        else
        {
            if (!marcus_waveform_build(wave, stereo, frames))
                return 0;
            info->seconds = marcus_waveform_seconds(wave);
            info->kind = MARCUS_WAVEFORM_SAMPLED;
        }
    }
    else if (status)
    {
        status = services->measure(services->context, &info->seconds);
        if (status < 0)
            return status;
        if (status)
            info->kind = MARCUS_WAVEFORM_SAMPLED;
    }
    info->resolved_source = media.source;
    info->resolved_type = media.type;
    info->resolved_chunk = media.number;
    return 1;
}

int marcus_waveform_cache_length(MarcusWaveformCacheState *cache, const MarcusWaveformRequest *request,
                                 const MarcusWaveformServices *services, MarcusWaveformInfo *info)
{
    WaveLength value, *entry;
    size_t index;
    int status;
    if (request->silent)
    {
        silent_info(info, 0);
        return 1;
    }
    value.key = request_key(request);
    if (cache->length_capacity)
    {
        index = length_index(cache->lengths, cache->length_capacity, &value.key);
        entry = cache->lengths[index];
        if (entry)
        {
            *info = entry->info;
            return 1;
        }
    }
    status = load(services, NULL, &value.info);
    if (status <= 0)
        return status;
    if (cache->length_count == cache->length_capacity / 2 && !grow_lengths(cache))
        return 0;
    entry = (WaveLength *)malloc(sizeof(*entry));
    if (!entry)
        return 0;
    *entry = value;
    index = length_index(cache->lengths, cache->length_capacity, &entry->key);
    cache->lengths[index] = entry;
    ++cache->length_count;
    *info = entry->info;
    return 1;
}

static size_t peak_bytes(const WavePeak *entry)
{
    return entry->envelope.leaves * 2 * sizeof(MarcusWaveformColumn);
}

static void drop_peak(MarcusWaveformCacheState *cache, WavePeak **slot)
{
    WavePeak *entry;
    entry = *slot;
    *slot = entry->next;
    cache->peak_bytes -= peak_bytes(entry);
    --cache->peak_count;
    marcus_waveform_dispose(&entry->envelope);
    free(entry);
}

void marcus_waveform_cache_clear(MarcusWaveformCacheState *cache)
{
    MarcusWaveformCacheState empty = {0};
    size_t i;
    while (cache->peaks)
        drop_peak(cache, &cache->peaks);
    for (i = 0; i < cache->length_capacity; ++i)
        free(cache->lengths[i]);
    free(cache->lengths);
    *cache = empty;
}

int marcus_waveform_cache_peaks(MarcusWaveformCacheState *cache, const MarcusWaveformRequest *request,
                                const MarcusWaveformServices *services, const MarcusWaveform **wave,
                                MarcusWaveformInfo *info)
{
    WaveKey key;
    WavePeak *entry, **slot;
    int status;
    if (request->silent)
    {
        silent_info(info, 1);
        *wave = NULL;
        return 1;
    }
    key = request_key(request);
    for (slot = &cache->peaks; *slot && !same_key(&(*slot)->key, &key); slot = &(*slot)->next)
    {
    }
    entry = *slot;
    if (entry)
        *slot = entry->next;
    else
    {
        entry = (WavePeak *)calloc(1, sizeof(*entry));
        if (!entry)
            return 0;
        entry->key = key;
        status = load(services, &entry->envelope, &entry->info);
        if (status <= 0)
        {
            marcus_waveform_dispose(&entry->envelope);
            free(entry);
            return status;
        }
        while (cache->peaks && (cache->peak_count >= peak_limit || peak_bytes(entry) > PEAK_BUDGET - cache->peak_bytes))
        {
            /* The most recently used entry is first. */
            for (slot = &cache->peaks; (*slot)->next; slot = &(*slot)->next)
            {
            }
            drop_peak(cache, slot);
        }
        cache->peak_bytes += peak_bytes(entry);
        ++cache->peak_count;
    }
    entry->next = cache->peaks;
    cache->peaks = entry;
    *info = entry->info;
    *wave = &entry->envelope;
    return 1;
}
