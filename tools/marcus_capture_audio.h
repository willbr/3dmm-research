#pragma once
#include <SDL3/SDL.h>
#include <algorithm>

/* The capture process supplies a real zero-valued stream even for silent
 * movies. Process loopback can then observe pre-Play silence before the engine
 * opens its lazy sound device. Authored audio remains on the player's stream. */
class MarcusCaptureSilence
{
    SDL_AudioStream *stream = nullptr;
    bool initialized = false;
    static void SDLCALL Feed(void *, SDL_AudioStream *output, int additional, int)
    {
        const float zeros[2048] = {};
        while (additional > 0)
        {
            const int count = std::min(additional, int(sizeof(zeros)));
            if (!SDL_PutAudioStreamData(output, zeros, count))
                break;
            additional -= count;
        }
    }

  public:
    MarcusCaptureSilence() = default;
    MarcusCaptureSilence(const MarcusCaptureSilence &) = delete;
    MarcusCaptureSilence &operator=(const MarcusCaptureSilence &) = delete;
    bool Open()
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return false;
        initialized = true;
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 48000};
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, nullptr);
        return stream && SDL_ResumeAudioStreamDevice(stream);
    }
    ~MarcusCaptureSilence()
    {
        SDL_DestroyAudioStream(stream);
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};
