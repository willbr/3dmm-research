/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "compressed_wave.h"
#include "compressed_wave_decode.h"
#include "compressed_audio_licenses.h"
#include <memory>
#include <new>
#include <utility>

namespace KauaiAudio
{
CompressedWaveResult DecodeCompressedWave(const void *data, std::size_t bytes, DecodedWave &decoded)
{
    MMDecodedWave pcm = {};
    const int encoding = mm_compressed_wave_decode(data, bytes, &pcm);
    if (encoding == MM_WAVE_NOT_HANDLED)
        return CompressedWaveResult::NotHandled;
    if (encoding == MM_WAVE_INVALID)
        return CompressedWaveResult::Invalid;
    const std::unique_ptr<MMDecodedWave, decltype(&mm_compressed_wave_dispose)> owner(&pcm, mm_compressed_wave_dispose);
    try
    {
        DecodedWave result;
        result.sampleRate = pcm.rate;
        result.channels = pcm.channels;
        result.samples.assign(pcm.samples, pcm.samples + pcm.count);
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
