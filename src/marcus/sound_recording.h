#ifndef MARCUS_SOUND_RECORDING_H
#define MARCUS_SOUND_RECORDING_H
#include "player.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusSoundRecording MarcusSoundRecording;
MarcusSoundRecording *sound_recording_create(void);
void sound_recording_destroy(MarcusSoundRecording *);
/* Default microphone, mono 48 kHz/16-bit PCM; max_ms is 1..180000.
 * Starting another take discards the previous take after capture starts. */
int sound_recording_start(MarcusSoundRecording *, unsigned long max_ms);
/* Poll: 1 while recording, 0 when stopped, -1 on failure. Stop/poll close
 * the capture device and prepare a private WAV, without editing a movie. */
int sound_recording_poll(MarcusSoundRecording *);
int sound_recording_stop(MarcusSoundRecording *);
const char *sound_recording_error(const MarcusSoundRecording *);
/* Borrowed path, valid until the next successful start or destruction.
 * Import through marcus_add_sound before releasing the recording. */
const char *sound_recording_path(const MarcusSoundRecording *);
int sound_recording_waveform(MarcusSoundRecording *, int count, MarcusWaveformColumn *, double *seconds);
#ifdef __cplusplus
}
#endif
#endif
