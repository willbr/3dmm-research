/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "compressed_wave.h"
#include "three_mm/wave.h"
#include "compressed_audio_licenses.h"
#include <gsm.h>
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
namespace KauaiAudio
{
namespace
{
constexpr std::size_t MaxSamples = INT_MAX / sizeof(std::int16_t);
using Wave = ThreeMMWaveView;
bool Gsm(const Wave &wave, DecodedWave &result)
{
    const auto blocks = wave.data_bytes / 65;
    const auto frames = blocks * 320;
    std::unique_ptr<gsm_state, decltype(&gsm_destroy)> state(gsm_create(), gsm_destroy);
    int enabled = 1;
    if (!state || gsm_option(state.get(), GSM_OPT_WAV49, &enabled) < 0)
        return false;
    result.samples.resize(frames);
    std::array<gsm_byte, 65> block;
    for (std::size_t at = 0, sample = 0; at < wave.data_bytes; at += 65, sample += 320)
    {
        std::memcpy(block.data(), wave.data + at, block.size());
        if (gsm_decode(state.get(), block.data(), result.samples.data() + sample) ||
            gsm_decode(state.get(), block.data() + 33, result.samples.data() + sample + 160))
            return false;
    }
    return true;
}
bool Mp3(const Wave &wave, DecodedWave &result)
{
    drmp3 decoder;
    if (!drmp3_init_memory(&decoder, wave.data, wave.data_bytes, nullptr))
        return false;
    const auto cleanup = [](drmp3 *mp3) { drmp3_uninit(mp3); };
    std::unique_ptr<drmp3, decltype(cleanup)> owned(&decoder, cleanup);
    if (decoder.channels != static_cast<unsigned>(result.channels) ||
        decoder.sampleRate != static_cast<unsigned>(result.sampleRate))
        return false;
    std::array<drmp3_int16, 4096 * 2> samples;
    for (;;)
    {
        const auto count = drmp3_read_pcm_frames_s16(&decoder, 4096, samples.data()) * decoder.channels;
        if (!count)
            break;
        if (count > MaxSamples - result.samples.size())
            return false;
        result.samples.insert(result.samples.end(), samples.data(), samples.data() + count);
    }
    return !result.samples.empty();
}
} // namespace
CompressedWaveResult DecodeCompressedWave(const void *data, std::size_t bytes, DecodedWave &decoded)
{
    Wave wave = {};
    ThreeMMI32 rate, channels;
    const auto encoding = three_mm_wave_compressed(data, bytes, &wave, &rate, &channels);
    if (encoding == THREE_MM_WAVE_NOT_HANDLED)
        return CompressedWaveResult::NotHandled;
    if (encoding == THREE_MM_WAVE_INVALID)
        return CompressedWaveResult::Invalid;
    try
    {
        DecodedWave result;
        result.sampleRate = static_cast<int>(rate);
        result.channels = channels;
        if (!(encoding == 0x31 ? Gsm(wave, result) : Mp3(wave, result)))
            return CompressedWaveResult::Invalid;
        // GSM blocks include padding samples; a valid fact count trims them.
        // MP3 duration comes from its stream, including any gapless metadata.
        const auto frames =
            three_mm_wave_trim_frames(encoding, wave.fact, static_cast<ThreeMMU32>(result.samples.size() / channels));
        result.samples.resize(std::size_t(frames) * channels);
        decoded = std::move(result);
        return CompressedWaveResult::Decoded;
    }
    catch (const std::bad_alloc &)
    {
        return CompressedWaveResult::Invalid;
    }
}
const char *CompressedWaveLicenseText()
{
    return CompressedAudioLicenses;
}
} // namespace KauaiAudio
