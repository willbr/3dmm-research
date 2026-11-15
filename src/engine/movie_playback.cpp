/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME

static_assert(kdtimSecond == MM_PLAYBACK_TICKS_PER_SECOND &&
                  kPlaybackFrameTicks == MM_PLAYBACK_FRAME_TICKS &&
                  kPlaybackScrollTicks == MM_PLAYBACK_SCROLL_TICKS &&
                  kPlaybackFadeTicks == MM_PLAYBACK_FADE_TICKS &&
                  kPlaybackFadeSeconds == MM_PLAYBACK_FADE_SECONDS && kdtsSecond == 1000,
              "Playback clock contract");
static_assert(int(witNil) == MM_PLAYBACK_WAIT_NONE && int(witUntilClick) == MM_PLAYBACK_WAIT_CLICK &&
                  int(witUntilSnd) == MM_PLAYBACK_WAIT_SOUND && int(witForTime) == MM_PLAYBACK_WAIT_TIME &&
                  transNil == -1,
              "Playback enum contract");

namespace
{
MMPlaybackReply BackendReply(const MoviePlaybackReply &reply)
{
    MMPlaybackReply result;
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
    mm_playback_init(&_backend, &state, started);
}
LegacyLong MoviePlaybackEvaluator::FrameDelay(LegacyUlong started, LegacyUlong now)
{
    return mm_playback_frame_delay(started, now);
}
MoviePlaybackCommand MoviePlaybackEvaluator::Next()
{
    MMPlaybackCommand command = {MM_PLAYBACK_FINISHED, 0, 0};
    const int success = mm_playback_next(&_backend, &command);
    Assert(success, "Unacknowledged playback command");
    return {static_cast<MoviePlaybackCommandKind>(command.kind), command.value, command.flag != 0};
}
void MoviePlaybackEvaluator::Complete(const MoviePlaybackReply &reply)
{
    const auto result = BackendReply(reply);
    const int success = mm_playback_complete(&_backend, &result);
    Assert(success, "No playback command to complete");
}
MoviePlayEvaluator::MoviePlayEvaluator(MoviePlaybackState &state)
{
    mm_play_init(&_backend, &state);
}
MoviePlayCommand MoviePlayEvaluator::Next()
{
    MMPlayCommand command = {MM_PLAY_FINISHED, 0};
    const int success = mm_play_next(&_backend, &command);
    Assert(success, "Unacknowledged play command");
    return {static_cast<MoviePlayCommandKind>(command.kind), command.flag != 0};
}
void MoviePlayEvaluator::Complete(const MoviePlaybackReply &reply)
{
    const auto result = BackendReply(reply);
    const int success = mm_play_complete(&_backend, &result);
    Assert(success, "No play command to complete");
}
