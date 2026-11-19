#ifndef MARCUS_WAVEFORM_CORE_H
#define MARCUS_WAVEFORM_CORE_H
#include "player.h"
#include "mm/types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusWaveform
{
    MarcusWaveformColumn *tree;
    size_t leaves, frames;
} MarcusWaveform;

/* Initialize owners to zero. Build borrows interleaved 48 kHz stereo for the
 * call and preserves the previous envelope on failure. */
int marcus_waveform_build(MarcusWaveform *, const float *stereo, size_t frames);
void marcus_waveform_dispose(MarcusWaveform *);
double marcus_waveform_seconds(const MarcusWaveform *);
void marcus_waveform_read(const MarcusWaveform *, double first, double step, int count,
                          MarcusWaveformColumn *, int repeat);
void marcus_waveform_read_timeline(const MarcusWaveform *, double start, double end, double first, double step,
                                   int count, MarcusWaveformColumn *, int repeat,
                                   double (*time_at)(const void *, double), const void *context);

typedef struct MarcusWaveformRequest
{
    MMI32 source;
    MMU32 type, number, child, scene;
    int silent;
} MarcusWaveformRequest;
typedef struct MarcusWaveformMedia
{
    MMI32 source;
    MMU32 type, number, last_midi_ms;
    int midi, silent;
} MarcusWaveformMedia;
typedef struct MarcusWaveformServices
{
    void *context;
    int (*open)(void *, MarcusWaveformMedia *, char *message, size_t capacity);
    int (*measure)(void *, double *seconds);
    int (*decode)(void *, const float **stereo, size_t *frames);
} MarcusWaveformServices;
typedef struct MarcusWaveformCacheState
{
    struct MarcusWaveformPeak *peaks;
    struct MarcusWaveformLength **lengths;
    size_t peak_count, peak_bytes, length_count, length_capacity;
} MarcusWaveformCacheState;

/* Services are synchronous and never reenter. They return 1 for success, 0
 * for unavailable media, or -1 for a host exception. Decoded samples are
 * borrowed until the call returns. Cache calls return 1 for a published
 * result (including unavailable media), 0 for allocation failure, or -1
 * for a host exception. A borrowed envelope lasts until the next mutation. */
void marcus_waveform_cache_clear(MarcusWaveformCacheState *);
int marcus_waveform_cache_length(MarcusWaveformCacheState *, const MarcusWaveformRequest *,
                                 const MarcusWaveformServices *, MarcusWaveformInfo *);
int marcus_waveform_cache_peaks(MarcusWaveformCacheState *, const MarcusWaveformRequest *,
                                const MarcusWaveformServices *, const MarcusWaveform **, MarcusWaveformInfo *);

#ifdef __cplusplus
}
#endif
#endif
