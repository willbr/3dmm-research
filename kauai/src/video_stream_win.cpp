/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "video_stream_host.h"
#include <new>
#include "frame.h"
ASSERTNAME
namespace KauaiVideo
{
namespace
{
class AviStreamHost final : public StreamHost
{
    PAVIFILE file = nullptr;
    PAVISTREAM stream = nullptr;
    PGETFRAME decoder = nullptr;
    HDRAWDIB drawing = nullptr;
    LegacyLong start = 0;
    StreamInfo info;
    void Close()
    {
        if (drawing)
            DrawDibClose(drawing);
        if (decoder)
            AVIStreamGetFrameClose(decoder);
        if (stream)
            AVIStreamRelease(stream);
        if (file)
            AVIFileRelease(file);
        drawing = nullptr;
        decoder = nullptr;
        stream = nullptr;
        file = nullptr;
        start = 0;
        info = {};
    }

  public:
    AviStreamHost()
    {
        AVIFileInit();
    }
    ~AviStreamHost() override
    {
        Close();
        AVIFileExit();
    }
    bool Open(Filename *filename) override
    {
        Close();
        if (!filename)
            return false;
        String path;
        filename->GetStnPath(&path);
        AVIFILEINFO description;
        if (AVIFileOpen(&file, path.Psz(), OF_READ | OF_SHARE_DENY_WRITE, nullptr) != 0)
        {
            file = nullptr;
            return false;
        }
        if (AVIFileGetStream(file, &stream, streamtypeVIDEO, 0) != 0)
        {
            stream = nullptr;
            Close();
            return false;
        }
        if (!(decoder = AVIStreamGetFrameOpen(stream, nullptr)) ||
            AVIFileInfo(file, &description, sizeof(description)) != 0)
        {
            Close();
            return false;
        }
        const auto length = AVIStreamLength(stream), first = AVIStreamStart(stream);
        if (length < 0 || first < 0 || !(drawing = DrawDibOpen()))
        {
            Close();
            return false;
        }
        info = {length, static_cast<LegacyLong>(description.dwWidth), static_cast<LegacyLong>(description.dwHeight)};
        start = first;
        return true;
    }
    StreamInfo Info() const override
    {
        return info;
    }
    LegacyLong TimeForFrame(LegacyLong frame) override
    {
        const auto sample = std::int64_t(frame) + start;
        return stream && sample >= INT32_MIN && sample <= INT32_MAX
                   ? AVIStreamSampleToTime(stream, static_cast<LONG>(sample))
                   : -1;
    }
    LegacyLong FrameForTime(LegacyUlong milliseconds) override
    {
        return stream ? AVIStreamTimeToSample(stream, static_cast<LONG>(milliseconds)) - start : -1;
    }
    bool DrawFrame(LegacyLong frame, GraphicsEnvironment *environment, RC *rectangle) override
    {
        if (!decoder || !environment || !rectangle || frame < 0 || frame >= info.frames)
            return false;
        const auto sample = std::int64_t(frame) + start;
        if (sample > INT32_MAX)
            return false;
        auto pixels = static_cast<BITMAPINFOHEADER *>(AVIStreamGetFrame(decoder, static_cast<LONG>(sample)));
        if (!pixels)
            return false;
        environment->DrawDib(drawing, pixels, rectangle);
        return true;
    }
};
} // namespace
StreamHost *NewStreamHost()
{
    return new (std::nothrow) AviStreamHost;
}
} // namespace KauaiVideo
