/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "recording_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WINDOWS_
#error Recording data must not import the Windows SDK
#endif

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "Recording data failed: %d\n", __LINE__); exit(1); } } while (0)

static MMU32 dword(const unsigned char *p)
{
    return p[0] | ((MMU32)p[1] << 8) | ((MMU32)p[2] << 16) | ((MMU32)p[3] << 24);
}

int main(void)
{
    MMRecordingBuffer capture = {0};
    MMWaveBuffer wave;
    unsigned char input[2008 * 4], saved[2048], *large;
    MMU32 bytes = 3, saved_bytes;
    unsigned i, at, channel, value;
    CHECK(mm_wave_buffer_init(&wave, NULL) && mm_wave_buffer_reserve(&wave, bytes));
    memcpy(wave.data, "abc", bytes);
    CHECK(!mm_recording_full(&capture) && !mm_recording_frames(&capture) &&
          !mm_recording_wave(&capture, &wave, &bytes) && bytes == 3 && !memcmp(wave.data, "abc", bytes));
    CHECK(!mm_recording_init(&capture, 0, 1, 8, 1000) && !mm_recording_init(&capture, 1000, 0, 8, 1000) &&
          !mm_recording_init(&capture, 1000, 1, 24, 1000) && !mm_recording_init(&capture, 1000, 1, 8, 0xffffffffUL));
    CHECK(mm_recording_init(&capture, 1000, 1, 8, 2008));
    memset(input, 160, 2008);
    for (i = 600; i < 800; ++i)
        input[i] = (i & 1) ? 180 : 140;
    memset(input + 2000, 255, 8);
    CHECK(mm_recording_append(&capture, input, 317) == 317 && mm_recording_frames(&capture) == 317 &&
          !mm_recording_full(&capture));
    CHECK(mm_recording_append(&capture, input + 317, 2008 - 317) == 2008 - 317 && mm_recording_full(&capture));
    CHECK(!mm_recording_append(&capture, input, 2008) && mm_recording_frames(&capture) == 2008);
    CHECK(!mm_recording_init(&capture, 1000, 3, 8, 1) && mm_recording_full(&capture));
    CHECK(mm_recording_wave(&capture, &wave, &bytes) && dword(wave.data + 40) == 949 && dword(wave.data + 24) == 1000);
    for (i = 0; i < 949; ++i)
        CHECK(wave.data[44 + i] == (i >= 249 && i < 449 ? (((i + 351) & 1) ? 148 : 108) : 128));
    CHECK(bytes <= sizeof(saved));
    saved_bytes = bytes;
    memcpy(saved, wave.data, bytes);
    CHECK(mm_recording_wave(&capture, &wave, &bytes) && bytes == saved_bytes && !memcmp(wave.data, saved, bytes));
    mm_recording_reset(&capture);
    CHECK(!mm_recording_frames(&capture) && !mm_recording_full(&capture));
    CHECK(!mm_recording_wave(&capture, &wave, &bytes) && bytes == saved_bytes && !memcmp(wave.data, saved, bytes));
    CHECK(mm_recording_append(&capture, input, 8) == 8 && !mm_recording_wave(&capture, &wave, &bytes) &&
          bytes == saved_bytes && !memcmp(wave.data, saved, bytes));
    mm_recording_reset(&capture);
    memset(input, 160, 1008);
    CHECK(mm_recording_append(&capture, input, 1008) == 1008 && mm_recording_wave(&capture, &wave, &bytes));
    CHECK(dword(wave.data + 40) == 1000);
    for (i = 44; i < bytes; ++i)
        CHECK(wave.data[i] == 128);

    CHECK(mm_recording_init(&capture, 1000, 2, 16, 2008));
    for (i = 0; i < 2008; ++i)
        for (channel = 0; channel < 2; ++channel)
        {
            value = (unsigned)(i >= 600 && i < 800 ? (channel ? -2000 : 6000) : 2000);
            at = 4 * i + 2 * channel;
            input[at] = (unsigned char)value;
            input[at + 1] = (unsigned char)(value >> 8);
        }
    CHECK(!mm_recording_append(&capture, input, 3) && !mm_recording_frames(&capture));
    CHECK(mm_recording_append(&capture, input, sizeof(input)) == sizeof(input) && mm_recording_wave(&capture, &wave, &bytes));
    CHECK(wave.data[22] == 2 && wave.data[32] == 4 && wave.data[34] == 16 && dword(wave.data + 28) == 4000 &&
          dword(wave.data + 40) == 3796);
    for (i = 0; i < 949; ++i)
        for (channel = 0; channel < 2; ++channel)
        {
            at = 44 + i * 4 + channel * 2;
            value = wave.data[at] | ((unsigned)wave.data[at + 1] << 8);
            CHECK(value == (i >= 249 && i < 449 ? (channel ? 61536u : 4000u) : 0));
        }

    /* The full quarter-second bias sum exceeds signed 32-bit range. */
    CHECK(mm_recording_init(&capture, 192000, 2, 16, 251));
    large = malloc(48008u * 4);
    CHECK(large);
    for (i = 0; i < 48008u * 4; i += 2)
    {
        large[i] = 0;
        large[i + 1] = 128;
    }
    CHECK(mm_recording_append(&capture, large, 48008u * 4) == 48008u * 4);
    CHECK(mm_recording_wave(&capture, &wave, &bytes) && bytes == 44 + 48000u * 4);
    for (i = 44; i < bytes; ++i)
        CHECK(!wave.data[i]);
    free(large);
    mm_wave_buffer_dispose(&wave);
    mm_recording_dispose(&capture);
    puts("C89 recording: bounded capture, reset, transactional WAVE, AudioMan bias and trim, and stereo LE16 passed.");
    return 0;
}
