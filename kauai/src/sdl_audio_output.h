#ifndef KAUAI_SDL_AUDIO_OUTPUT_H
#define KAUAI_SDL_AUDIO_OUTPUT_H

#include "audio_mixer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMSdlAudioOutput MMSdlAudioOutput;
typedef void (*MMSdlAudioRender)(void *context, float *stereo, size_t frames);
typedef void (*MMSdlAudioOutputError)(void *context, int initializing, const char *detail);

/* Main-thread device lifetime. Context is borrowed through shutdown, which
 * SDL serializes with render. The frame limit is in the range 1 to 512. */
MMSdlAudioOutput *mm_sdl_audio_output_create(void *context, MMSdlAudioRender, size_t frame_limit);
MMSdlAudioOutput *mm_sdl_audio_output_create_mixer(void);
void mm_sdl_audio_output_destroy(MMSdlAudioOutput *);
int mm_sdl_audio_output_open(MMSdlAudioOutput *, void *error_context, MMSdlAudioOutputError);
/* Close the stream and wait for render, keeping SDL initialized for a reopen. */
void mm_sdl_audio_output_close(MMSdlAudioOutput *);
/* Do not hold the render callback's lock while clearing the stream. */
void mm_sdl_audio_output_clear(MMSdlAudioOutput *);
/* Only the PCM mixer factory supplies a device mixer. */
MMAudioDeviceMixer *mm_sdl_audio_output_mixer(MMSdlAudioOutput *);

#ifdef __cplusplus
}
#endif
#endif
