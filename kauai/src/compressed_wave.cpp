/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "compressed_wave.h"
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
unsigned U16(const std::uint8_t *p)
{
    return p[0] | (unsigned(p[1]) << 8);
}
std::uint32_t U32(const std::uint8_t *p)
{
    return U16(p) | (std::uint32_t(U16(p + 2)) << 16);
}
struct Wave
{
    const std::uint8_t *format = nullptr, *data = nullptr;
    std::size_t formatBytes = 0, dataBytes = 0;
    std::uint32_t fact = 0;
};
bool Parse(const std::uint8_t *p, std::size_t bytes, Wave &wave)
{
    if (!p || bytes < 12 || std::memcmp(p, "RIFF", 4) || std::memcmp(p + 8, "WAVE", 4))
        return false;
    const std::uint64_t end = std::uint64_t(U32(p + 4)) + 8;
    if (end < 12 || end > bytes)
        return false;
    for (std::uint64_t at = 12; at < end;)
    {
        if (end - at < 8)
            return false;
        const auto count = U32(p + at + 4);
        if (count > end - at - 8)
            return false;
        if (!std::memcmp(p + at, "fmt ", 4) && !wave.format)
        {
            wave.format = p + at + 8;
            wave.formatBytes = count;
        }
        else if (!std::memcmp(p + at, "data", 4) && !wave.data)
        {
            wave.data = p + at + 8;
            wave.dataBytes = count;
        }
        else if (!std::memcmp(p + at, "fact", 4) && count >= 4)
            wave.fact = U32(p + at + 8);
        at += 8 + std::uint64_t(count);
        // Some older WAV writers omit the last odd-sized chunk's pad byte.
        if ((count & 1) && at < end)
            ++at;
    }
    return wave.format && wave.formatBytes >= 16 && wave.data && wave.dataBytes;
}
bool Gsm(const Wave &wave, DecodedWave &result)
{
    if (result.channels != 1 || wave.formatBytes < 20 || U16(wave.format + 12) != 65 || U16(wave.format + 14) != 0 ||
        U16(wave.format + 16) < 2 || U16(wave.format + 18) != 320 || wave.dataBytes % 65)
        return false;
    const auto blocks = wave.dataBytes / 65;
    if (blocks > MaxSamples / 320)
        return false;
    const auto frames = blocks * 320;
    std::unique_ptr<gsm_state, decltype(&gsm_destroy)> state(gsm_create(), gsm_destroy);
    int enabled = 1;
    if (!state || gsm_option(state.get(), GSM_OPT_WAV49, &enabled) < 0)
        return false;
    result.samples.resize(frames);
    std::array<gsm_byte, 65> block;
    for (std::size_t at = 0, sample = 0; at < wave.dataBytes; at += 65, sample += 320)
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
    if (!drmp3_init_memory(&decoder, wave.data, wave.dataBytes, nullptr))
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
    Wave wave;
    if (!Parse(static_cast<const std::uint8_t *>(data), bytes, wave))
        return CompressedWaveResult::NotHandled;
    const auto encoding = U16(wave.format);
    if (encoding != 0x31 && encoding != 0x55)
        return CompressedWaveResult::NotHandled;
    const auto rate = U32(wave.format + 4), channels = U16(wave.format + 2);
    if (!rate || rate > INT_MAX || !channels || channels > 2)
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
        if (encoding == 0x31 && wave.fact && wave.fact < result.samples.size() / channels)
            result.samples.resize(std::size_t(wave.fact) * channels);
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
