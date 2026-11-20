/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "avi_movie.h"
#include <new>
#include <utility>

namespace KauaiVideo
{
bool DecodeVideo1(const void *data, std::size_t bytes, std::int32_t width, std::int32_t height,
                  std::vector<std::uint8_t> &pixels)
{
    return mm_avi_decode_video1(data, bytes, width, height, pixels.data(), pixels.size()) != 0;
}

AviMovie::AviMovie() = default;
AviMovie::~AviMovie()
{
    mm_avi_close(data);
}
void AviMovie::Reset()
{
    mm_avi_close(data);
    data = nullptr;
}
bool AviMovie::Read(const void *source, std::size_t bytes)
{
    return mm_avi_read(&data, source, bytes, nullptr) != 0;
}
AviInfo AviMovie::Info() const
{
    return mm_avi_info(data);
}
AviAudio AviMovie::Audio() const
{
    return mm_avi_audio(data);
}
std::int32_t AviMovie::TimeForFrame(std::int32_t frame) const
{
    return mm_avi_time_for_frame(data, frame);
}
std::int32_t AviMovie::FrameForTime(std::int32_t milliseconds) const
{
    return mm_avi_frame_for_time(data, milliseconds);
}
bool AviMovie::DecodeFrame(std::int32_t frame, KauaiPixels::DibFrame &output)
{
    const auto bytes = mm_avi_frame_bytes(data, frame);
    if (!bytes)
        return false;
    try
    {
        const auto info = mm_avi_info(data);
        KauaiPixels::DibFrame next;
        next.width = info.width;
        next.height = info.height;
        next.pitch = info.width * 4;
        next.pixels.resize(bytes);
        if (!mm_avi_decode_frame(data, frame, next.pixels.data(), next.pixels.size()))
            return false;
        output = std::move(next);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
} // namespace KauaiVideo
