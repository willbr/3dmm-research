/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio.h"
#include "sdl_wave.h"
#include <new>

namespace KauaiAudio
{
int ImportWave(void *context, const MMReadStream *source, MMI32 style, const MMChunkNameValue *name, MMU32 *number,
               const MMArchiveWriter *writer)
{
    return mm_sdl_wave_import(context, source, style, name, number, writer);
}

bool PrepareWaveImport(const void *data, std::size_t bytes, std::vector<std::uint8_t> &wave)
{
    MMWaveBuffer scratch;
    mm_wave_buffer_init(&scratch, nullptr);
    bool ok = false;
    try
    {
        const unsigned char *selected;
        MMU32 count;
        if (mm_sdl_wave_prepare(data, bytes, &scratch, &selected, &count))
        {
            std::vector<std::uint8_t> result(selected, selected + count);
            wave.swap(result);
            ok = true;
        }
    }
    catch (const std::bad_alloc &)
    {
    }
    mm_wave_buffer_dispose(&scratch);
    return ok;
}

bool WaveSeconds(const void *data, std::size_t bytes, double &seconds)
{
    return mm_sdl_wave_seconds(data, bytes, &seconds) != 0;
}

bool DecodeWave(const void *data, std::size_t bytes, Sample &sample)
{
    MMAudioSample *owner;
    if (!mm_sdl_wave_decode(data, bytes, &owner))
        return false;
    sample.reset(owner);
    return true;
}
} // namespace KauaiAudio
