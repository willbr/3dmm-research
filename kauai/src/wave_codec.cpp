/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "wave_codec.h"
#include "three_mm/wave.h"
#include <climits>
#include <new>
namespace KauaiAudio
{
namespace
{
bool Encode(const std::vector<std::uint8_t> &samples, std::uint32_t rate, unsigned channels, unsigned bits, bool adpcm,
            std::vector<std::uint8_t> &wave)
{
    ThreeMMU32 bytes;
    if (samples.size() > INT_MAX || !three_mm_wave_encode(samples.data(), static_cast<ThreeMMU32>(samples.size()), rate,
                                                          channels, bits, adpcm, nullptr, 0, &bytes))
        return false;
    try
    {
        std::vector<std::uint8_t> result(bytes);
        if (!three_mm_wave_encode(samples.data(), static_cast<ThreeMMU32>(samples.size()), rate, channels, bits, adpcm,
                                  result.data(), bytes, &bytes))
            return false;
        wave.swap(result);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace
bool Pcm8Wave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, std::vector<std::uint8_t> &wave)
{
    return Encode(samples, rate, 1, 8, false, wave);
}
bool PcmWave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, unsigned channels, unsigned bits,
             std::vector<std::uint8_t> &wave)
{
    return Encode(samples, rate, channels, bits, false, wave);
}
bool AdpcmWave(const std::vector<std::uint8_t> &samples, std::uint32_t rate, std::vector<std::uint8_t> &wave)
{
    return Encode(samples, rate, 1, 8, true, wave);
}
bool LegacyImportWave(const std::vector<std::uint8_t> &samples, std::vector<std::uint8_t> &wave)
{
    return samples.size() <= INT_MAX &&
           Encode(samples, 11025, 1, 8, three_mm_wave_import_adpcm(static_cast<ThreeMMU32>(samples.size())) != 0, wave);
}
bool LegacyWaveFormat(const void *data, std::size_t bytes)
{
    return three_mm_wave_legacy_format(data, bytes) != 0;
}
bool WaveFactFrames(const void *data, std::size_t bytes, std::uint32_t &frames)
{
    ThreeMMU32 result;
    if (!three_mm_wave_fact_frames(data, bytes, &result))
        return false;
    frames = result;
    return true;
}
} // namespace KauaiAudio
