#ifndef KAUAI_AUDIO_SAMPLE_H
#define KAUAI_AUDIO_SAMPLE_H

#include "mm/audio_mixer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMAudioSample MMAudioSample;

/* Adopt interleaved stereo PCM and replace nonfinite values with silence.
 * Success takes ownership of the buffer. Failure leaves the buffer untouched. */
MMAudioSample *mm_audio_sample_adopt(float *stereo, size_t frames, void (*free_buffer)(void *));
/* Reference operations are thread-safe. The caller must already hold a live reference. */
void mm_audio_sample_retain(const void *sample);
void mm_audio_sample_release(const void *sample);
const float *mm_audio_sample_data(const MMAudioSample *sample);
size_t mm_audio_sample_frames(const MMAudioSample *sample);
/* Return a borrowed voice. The caller must keep the sample live through sync. */
MMAudioVoice mm_audio_sample_voice(const MMAudioSample *sample, MMI32 id, MMI32 plays);

#ifdef __cplusplus
}
#endif
#endif
