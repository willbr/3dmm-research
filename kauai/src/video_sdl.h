/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_VIDEO_SDL_H
#define KAUAI_VIDEO_SDL_H
#include "avi_movie.h"
namespace KauaiVideo
{
struct SDLVideoStatistics
{
    std::uint64_t opened, started, completed, decoded, presented, queuedAudioBytes, consumedAudioBytes,
        audibleAudioFrames;
};
SDLVideoStatistics SDLVideoStats();
void SDLVideoFramePresented(); // Graphics bridge records successful canvas draws.
// UI-thread player. Audio and video share the SDL stream consumption clock;
// silent representations use SDL's monotonic clock. No window/graphics API.
class SDLPlayback
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    SDLPlayback();
    ~SDLPlayback();
    // Opening closes the old movie even on failure. Input bytes are copied.
    bool Open(const void *, std::size_t, bool withAudio);
    void Close();
    AviInfo Info() const;
    std::int32_t TimeForFrame(std::int32_t) const;
    std::int32_t FrameForTime(std::int32_t) const;
    bool DecodeFrame(std::int32_t, KauaiPixels::DibFrame &);
    bool Play(std::int32_t frame);
    bool SetGain(float gain); // Per-player master volume; accepts finite 0..2.
    void Stop();
    bool Playing();
    std::int32_t Position();
};
} // namespace KauaiVideo
#endif
