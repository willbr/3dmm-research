/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include "sdl_recording.h"
#include "sdl_wave.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "SDL recording failed: %d\n", __LINE__); exit(1); } } while (0)

int main(void)
{
    MMSdlRecorder *recorder = NULL;
    MMSdlRecordingStats stats;
    const unsigned char *wave;
    const float *pcm;
    MMAudioSample *sample;
    MMU32 bytes, started;
    size_t frames, i;
    unsigned bits, channels;
    CHECK(!mm_sdl_recorder_start(recorder) && !mm_sdl_recorder_wave(recorder, NULL) &&
          !mm_sdl_recorder_play(recorder, 1.f) && mm_sdl_recorder_stop(recorder));
    CHECK(!mm_sdl_recorder_open(&recorder, 0, 1, 8, 50));
    for (bits = 8; bits <= 16; bits += 8)
        for (channels = 1; channels <= 2; ++channels)
        {
            CHECK(mm_sdl_recorder_open(&recorder, 11025, channels, bits, 50) && mm_sdl_recorder_start(recorder) &&
                  !mm_sdl_recorder_start(recorder));
            started = (MMU32)SDL_GetTicks();
            while (mm_sdl_recorder_recording(recorder) && (MMU32)SDL_GetTicks() - started < 5000)
                SDL_Delay(1);
            CHECK(!mm_sdl_recorder_recording(recorder));
            wave = mm_sdl_recorder_wave(recorder, &bytes);
            CHECK(wave && wave[20] == 1 && wave[22] == channels && wave[34] == bits);
            CHECK(bytes == 44 + ((551 - 8) * channels * (bits / 8) + 1) / 2 * 2);
            CHECK(!mm_sdl_recorder_open(&recorder, 11025, 3, bits, 50) && mm_sdl_recorder_wave(recorder, NULL) == wave);
            CHECK(mm_sdl_wave_decode(wave, bytes, &sample));
            frames = mm_audio_sample_frames(sample);
            pcm = mm_audio_sample_data(sample);
            CHECK(frames);
            for (i = 0; i < frames * 2; ++i)
                CHECK(pcm[i] == 0);
            mm_audio_sample_release(sample);
            CHECK(mm_sdl_recorder_play(recorder, 1.f));
            while (mm_sdl_recorder_playing(recorder) && (MMU32)SDL_GetTicks() - started < 5000)
                SDL_Delay(1);
            CHECK(!mm_sdl_recorder_playing(recorder) && mm_sdl_recorder_wave(recorder, NULL));
            CHECK(mm_sdl_recorder_play(recorder, 1.f) && mm_sdl_recorder_stop(recorder) &&
                  !mm_sdl_recorder_playing(recorder));
        }
    CHECK(mm_sdl_recorder_open(&recorder, 11025, 1, 8, 3000) && mm_sdl_recorder_start(recorder));
    SDL_Delay(120);
    CHECK(mm_sdl_recorder_stop(recorder) && !mm_sdl_recorder_recording(recorder) && mm_sdl_recorder_wave(recorder, NULL));
    CHECK(mm_sdl_recorder_start(recorder) && !mm_sdl_recorder_wave(recorder, NULL) && mm_sdl_recorder_stop(recorder));
    mm_sdl_recorder_stats(&stats);
    CHECK(stats.opened.low == 5 && stats.started.low == 6 && stats.completed.low >= 5 && stats.previewed.low == 8 &&
          stats.captured_frames.low >= 2204);
    mm_sdl_recorder_destroy(recorder);
    puts("C89 SDL recording: dummy-device capture, four PCM layouts, failed reopen, repeated takes and preview passed.");
    return 0;
}
