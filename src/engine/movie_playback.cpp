/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME

static_assert(kdtimSecond == THREE_MM_PLAYBACK_TICKS_PER_SECOND &&
                  kPlaybackFrameTicks == THREE_MM_PLAYBACK_FRAME_TICKS &&
                  kPlaybackScrollTicks == THREE_MM_PLAYBACK_SCROLL_TICKS &&
                  kPlaybackFadeTicks == THREE_MM_PLAYBACK_FADE_TICKS &&
                  kPlaybackFadeSeconds == THREE_MM_PLAYBACK_FADE_SECONDS && kdtsSecond == 1000,
              "Playback clock contract");
static_assert(int(witNil) == THREE_MM_PLAYBACK_WAIT_NONE && int(witUntilClick) == THREE_MM_PLAYBACK_WAIT_CLICK &&
                  int(witUntilSnd) == THREE_MM_PLAYBACK_WAIT_SOUND && int(witForTime) == THREE_MM_PLAYBACK_WAIT_TIME &&
                  transNil == -1,
              "Playback enum contract");

namespace
{
ThreeMMPlaybackReply BackendReply(const MoviePlaybackReply &reply)
{
    ThreeMMPlaybackReply result;
    result.success = reply.success;
    result.value = reply.value;
    result.number = reply.number;
    result.timestamp = reply.timestamp;
    result.position = {reply.position.scene, reply.position.sceneCount, reply.position.frame, reply.position.lastFrame,
                       int(reply.position.transition)};
    return result;
}
} // namespace
MoviePlaybackEvaluator::MoviePlaybackEvaluator(MoviePlaybackState &state, LegacyUlong started)
{
    three_mm_playback_init(&_backend, &state, started);
}
LegacyLong MoviePlaybackEvaluator::FrameDelay(LegacyUlong started, LegacyUlong now)
{
    return three_mm_playback_frame_delay(started, now);
}
MoviePlaybackCommand MoviePlaybackEvaluator::Next()
{
    ThreeMMPlaybackCommand command = {THREE_MM_PLAYBACK_FINISHED, 0, 0};
    const int success = three_mm_playback_next(&_backend, &command);
    Assert(success, "Unacknowledged playback command");
    return {static_cast<MoviePlaybackCommandKind>(command.kind), command.value, command.flag != 0};
}
void MoviePlaybackEvaluator::Complete(const MoviePlaybackReply &reply)
{
    const auto result = BackendReply(reply);
    const int success = three_mm_playback_complete(&_backend, &result);
    Assert(success, "No playback command to complete");
}
MoviePlayEvaluator::MoviePlayEvaluator(MoviePlaybackState &state)
{
    three_mm_play_init(&_backend, &state);
}
MoviePlayCommand MoviePlayEvaluator::Next()
{
    ThreeMMPlayCommand command = {THREE_MM_PLAY_FINISHED, 0};
    const int success = three_mm_play_next(&_backend, &command);
    Assert(success, "Unacknowledged play command");
    return {static_cast<MoviePlayCommandKind>(command.kind), command.flag != 0};
}
void MoviePlayEvaluator::Complete(const MoviePlaybackReply &reply)
{
    const auto result = BackendReply(reply);
    const int success = three_mm_play_complete(&_backend, &result);
    Assert(success, "No play command to complete");
}
