/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_VIDEO_STREAM_HOST_H
#define KAUAI_VIDEO_STREAM_HOST_H
#include "legacy_types.h"
class Filename;
class GraphicsEnvironment;
class RC;
namespace KauaiVideo
{
struct StreamInfo
{
    LegacyLong frames = 0, width = 0, height = 0;
};
// UI-thread presentation service. Implementations own decoder, drawing and
// platform lifetime. Relative frame indices keep native stream offsets private.
// Opening a new source closes the previous source, including on failure.
class StreamHost
{
  public:
    virtual ~StreamHost() = default;
    virtual bool Open(Filename *) = 0;
    virtual StreamInfo Info() const = 0;
    // Milliseconds on the source timeline; relative frame zero may have a
    // nonzero timestamp. The scheduler uses this mapping in both directions.
    virtual LegacyLong TimeForFrame(LegacyLong frame) = 0;
    virtual LegacyLong FrameForTime(LegacyUlong milliseconds) = 0;
    virtual bool DrawFrame(LegacyLong frame, GraphicsEnvironment *, RC *) = 0;
    // Optional device-clock playback. Native silent streams retain the shared
    // legacy scheduler; SDL's audio/video player supplies its own position.
    virtual bool ControlsPlayback() const
    {
        return false;
    }
    virtual bool Start(LegacyLong)
    {
        return true;
    }
    virtual void Stop()
    {
    }
    virtual bool Playing()
    {
        return false;
    }
    virtual LegacyLong Position()
    {
        return 0;
    }
};
StreamHost *NewStreamHost(); // Owned by caller; null on allocation failure.
StreamHost *NewSDLStreamHost(bool withAudio);
struct WindowState; // Private native-window implementation, not a window token.
} // namespace KauaiVideo
#endif
