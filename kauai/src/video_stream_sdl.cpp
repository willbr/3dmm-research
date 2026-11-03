/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "video_sdl.h"
#include "video_stream_host.h"
#include <new>
#include <vector>
#include "frame.h"
ASSERTNAME
namespace KauaiVideo
{
namespace
{
class SDLStreamHost final : public StreamHost
{
    SDLPlayback player;
    KauaiPixels::DibFrame image;
    LegacyLong cached = -1;
    bool audio;
    bool trueColorRequested = false;
    void ReleaseDrawingRequest()
    {
        if (trueColorRequested && vpappb)
            vpappb->RequestTrueColorOffscreen(false);
        trueColorRequested = false;
    }
    void Volume()
    {
        player.SetGain(vpsndm ? static_cast<float>(vpsndm->VlmCur()) / 65536.0f : 1.0f);
    }

  public:
    explicit SDLStreamHost(bool withAudio) : audio(withAudio)
    {
    }
    ~SDLStreamHost() override
    {
        ReleaseDrawingRequest();
    }
    bool Open(Filename *name) override
    {
        player.Close();
        ReleaseDrawingRequest();
        cached = -1;
        image = {};
        if (!name)
            return false;
        auto file = FileObject::PfilOpen(name);
        if (!file)
            return false;
        const auto count = file->FpMac();
        bool success = false;
        if (count > 0 && count <= 256 * 1024 * 1024)
        {
            try
            {
                std::vector<std::uint8_t> bytes(count);
                success = file->FReadRgb(bytes.data(), count, 0) && player.Open(bytes.data(), count, audio);
            }
            catch (const std::bad_alloc &)
            {
            }
        }
        ReleasePpo(&file);
        if (success && vpappb)
        {
            vpappb->RequestTrueColorOffscreen(true);
            trueColorRequested = true;
        }
        return success;
    }
    StreamInfo Info() const override
    {
        const auto i = player.Info();
        return {i.frames, i.width, i.height};
    }
    LegacyLong TimeForFrame(LegacyLong frame) override
    {
        return player.TimeForFrame(frame);
    }
    LegacyLong FrameForTime(LegacyUlong time) override
    {
        return player.FrameForTime(static_cast<LegacyLong>(time));
    }
    bool DrawFrame(LegacyLong frame, GraphicsEnvironment *environment, RC *rectangle) override
    {
        if (!environment || !rectangle)
            return false;
        if (cached != frame)
        {
            if (!player.DecodeFrame(frame, image))
                return false;
            cached = frame;
        }
        if (!environment->DrawVideoFrame(image, rectangle))
            return false;
        SDLVideoFramePresented();
        return true;
    }
    bool ControlsPlayback() const override
    {
        return true;
    }
    bool Start(LegacyLong frame) override
    {
        Volume();
        return player.Play(frame);
    }
    void Stop() override
    {
        player.Stop();
    }
    bool Playing() override
    {
        Volume();
        return player.Playing();
    }
    LegacyLong Position() override
    {
        Volume();
        return player.Position();
    }
};
} // namespace
StreamHost *NewSDLStreamHost(bool withAudio)
{
    try
    {
        return new SDLStreamHost(withAudio);
    }
    catch (const std::bad_alloc &)
    {
        return nullptr;
    }
}
} // namespace KauaiVideo
