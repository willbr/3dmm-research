/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_AVI_MOVIE_H
#define KAUAI_AVI_MOVIE_H
#include "dib_frame.h"
#include "mm/avi_movie.h"
namespace KauaiVideo
{
// Original Studio's AVI 1.0 subset: indexed Video 1 and optional PCM audio.
// No window, codec installation, SDL, filesystem or platform SDK is required.
using AviInfo = MMAviInfo;
using AviAudio = MMAviAudio;
// Decode a complete 4x4-tiled indexed frame. Previous pixels are top-down;
// skip blocks retain them. Failure leaves them unchanged.
bool DecodeVideo1(const void *data, std::size_t bytes, std::int32_t width, std::int32_t height,
                  std::vector<std::uint8_t> &pixels);
class AviMovie
{
    MMAviMovie *data = nullptr;

  public:
    AviMovie();
    ~AviMovie();
    AviMovie(const AviMovie &) = delete;
    AviMovie &operator=(const AviMovie &) = delete;
    // Owns a copy. Rejects unsupported streams, malformed chunks, time overflow,
    // files over 256 MiB, over 256K frames and images over 16M pixels.
    // Failure preserves the prior movie. Compressed packets are checked on decode.
    bool Read(const void *, std::size_t);
    void Reset();
    AviInfo Info() const;
    AviAudio Audio() const; // PCM is borrowed until the next successful read or reset.
    // Matches the original VfW timeline's integer truncation. Indices are
    // relative to stream start, timestamps include stream start.
    std::int32_t TimeForFrame(std::int32_t) const;
    std::int32_t FrameForTime(std::int32_t milliseconds) const;
    // Replays delta frames when seeking backwards. Owns only one indexed
    // reference frame; returns top-down BGRX32. Failure preserves output.
    bool DecodeFrame(std::int32_t, KauaiPixels::DibFrame &);
};
} // namespace KauaiVideo
#endif
