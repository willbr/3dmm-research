/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "mm/playback.h"
#include "mm/graphics_transition.h"
#include <math.h>
#include <string.h>

typedef enum PlaybackPhase
{
    PB_START,
    PB_STOP,
    PB_READ_VOLUME,
    PB_SET_VOLUME,
    PB_FADE_ALARM,
    PB_SCROLL,
    PB_SCROLL_ALARM,
    PB_PRESENT,
    PB_DISPLAY,
    PB_QUERY_SCROLLING,
    PB_START_SCROLL_ALARM,
    PB_CHECK_PAUSE,
    PB_CLICK_ON,
    PB_QUERY_SOUND,
    PB_SOUND_ALARM,
    PB_TIMED_ALARM,
    PB_CLICK_OFF,
    PB_READ_TIME,
    PB_FRAME_ALARM,
    PB_READ_POSITION,
    PB_SWITCH_SCENE,
    PB_REPLAY_SCENE,
    PB_ADVANCE_FRAME,
    PB_RENDER,
    PB_DONE
} PlaybackPhase;

typedef enum PlayPhase
{
    PLAY_TOGGLE,
    PLAY_RENDER_STOP,
    PLAY_CHECK_SCENE,
    PLAY_RESET_SOUNDS,
    PLAY_QUERY_END,
    PLAY_REWIND,
    PLAY_BEGIN,
    PLAY_READ_TIME,
    PLAY_START_CLOCK,
    PLAY_PREPARE_SCENE,
    PLAY_NOTIFY_STOPPED,
    PLAY_CLOCK_FAILED,
    PLAY_PREPARE_FAILED,
    PLAY_DISABLE_ACCELERATORS,
    PLAY_QUERY_OPENING,
    PLAY_REFRESH_OPENING,
    PLAY_BACKGROUND_SOUND,
    PLAY_DISPATCH_SOUNDS,
    PLAY_DONE
} PlayPhase;

void mm_frame_pacer_init(MMFramePacer *pacer)
{
    memset(pacer, 0, sizeof(*pacer));
    pacer->fps = -1;
}
void mm_playback_state_init(MMPlaybackState *state)
{
    memset(state, 0, sizeof(*state));
    state->transition = -1;
    state->fadeVolume = -1;
}
void mm_playback_reply_init(MMPlaybackReply *reply)
{
    memset(reply, 0, sizeof(*reply));
    reply->success = 1;
    reply->position.transition = -1;
}
MMPlaybackCommandKind mm_playback_after_frame(const MMPlaybackPosition *position, int loop_scene)
{
    if (position->frame < position->lastFrame)
        return MM_PLAYBACK_ADVANCE_FRAME;
    if (loop_scene)
        return MM_PLAYBACK_REPLAY_SCENE;
    if (position->scene < position->sceneCount - 1)
        return MM_PLAYBACK_SWITCH_SCENE;
    return MM_PLAYBACK_FINISHED;
}
void mm_playback_init(MMPlaybackEvaluator *evaluator, MMPlaybackState *state,
                            MMPlaybackU32 frameStart)
{
    memset(evaluator, 0, sizeof(*evaluator));
    evaluator->state = state;
    evaluator->frameStart = frameStart;
    evaluator->phase = PB_START;
    evaluator->position.transition = -1;
}
void mm_play_init(MMPlayEvaluator *evaluator, MMPlaybackState *state)
{
    memset(evaluator, 0, sizeof(*evaluator));
    evaluator->state = state;
    evaluator->phase = PLAY_TOGGLE;
}

/* Preserve the legacy signed wrapping delta and multiply/divide rounding,
 * including its zero-quotient behavior, without 64-bit or signed overflow. */
MMPlaybackI32 mm_playback_frame_delay(MMPlaybackU32 frameStart, MMPlaybackU32 now)
{
    MMPlaybackU32 delta = now - frameStart;
    MMPlaybackU32 magnitude = delta <= 0x7fffffffUL ? delta : (MMPlaybackU32)(~delta) + 1U;
    MMPlaybackI32 ticks = (MMPlaybackI32)((magnitude / 50U) * 3U + (magnitude % 50U) * 3U / 50U);
    if (delta > 0x7fffffffUL)
        ticks = -ticks;
    if (((magnitude % 50U) * 3U) % 50U)
        ticks += ticks < 0 ? -1 : 1;
    return ticks < MM_PLAYBACK_FRAME_TICKS ? MM_PLAYBACK_FRAME_TICKS - ticks : 0;
}

static int playback_command(MMPlaybackEvaluator *evaluator, MMPlaybackCommand *command,
                            MMPlaybackCommandKind kind, MMPlaybackI32 value, int flag)
{
    command->kind = kind;
    command->value = value;
    command->flag = flag;
    evaluator->waiting = 1;
    return 1;
}

static int play_command(MMPlayEvaluator *evaluator, MMPlayCommand *command, MMPlayCommandKind kind,
                        int flag)
{
    command->kind = kind;
    command->flag = flag;
    evaluator->waiting = 1;
    return 1;
}

int mm_playback_next(MMPlaybackEvaluator *evaluator, MMPlaybackCommand *command)
{
    if (evaluator->waiting)
        return 0;
    for (;;)
    {
        switch (evaluator->phase)
        {
        case PB_START:
            if (evaluator->state->stopRequested)
                evaluator->phase = PB_STOP;
            else if (evaluator->state->fadeOriginalVolume != 0)
                evaluator->phase = PB_READ_VOLUME;
            else if (evaluator->state->scrolling)
            {
                evaluator->state->scrolling = 0;
                evaluator->phase = PB_SCROLL;
            }
            else if (!evaluator->state->pausing)
                evaluator->phase = PB_PRESENT;
            else
                evaluator->phase =
                    evaluator->state->wait == MM_PLAYBACK_WAIT_SOUND ? PB_CHECK_PAUSE : PB_CLICK_OFF;
            break;
        case PB_CHECK_PAUSE:
            if (evaluator->state->unpaced)
            {
                switch (evaluator->state->wait)
                {
                case MM_PLAYBACK_WAIT_CLICK:
                    ++evaluator->state->skippedClickPauses;
                    break;
                case MM_PLAYBACK_WAIT_TIME:
                    ++evaluator->state->skippedTimedPauses;
                    break;
                case MM_PLAYBACK_WAIT_SOUND:
                    ++evaluator->state->skippedSoundPauses;
                    break;
                default:
                    break;
                }
                evaluator->state->wait = MM_PLAYBACK_WAIT_NONE;
                evaluator->state->pausing = 0;
                evaluator->phase = PB_READ_TIME;
                break;
            }
            switch (evaluator->state->wait)
            {
            case MM_PLAYBACK_WAIT_CLICK:
                evaluator->state->wait = MM_PLAYBACK_WAIT_NONE;
                evaluator->state->pausing = 1;
                evaluator->phase = PB_CLICK_ON;
                break;
            case MM_PLAYBACK_WAIT_SOUND:
                evaluator->phase = PB_QUERY_SOUND;
                break;
            case MM_PLAYBACK_WAIT_TIME:
                evaluator->state->wait = MM_PLAYBACK_WAIT_NONE;
                evaluator->state->pausing = 1;
                evaluator->phase = PB_TIMED_ALARM;
                break;
            case MM_PLAYBACK_WAIT_NONE:
                evaluator->state->pausing = 0;
                evaluator->phase = PB_READ_TIME;
                break;
            default:
                /* Preserve legacy recovery from an invalid wait reason. */
                evaluator->phase = PB_READ_TIME;
                break;
            }
            break;
        case PB_STOP:
            return playback_command(evaluator, command, MM_PLAYBACK_STOP, 0, 0);
        case PB_READ_VOLUME:
            return playback_command(evaluator, command, MM_PLAYBACK_READ_VOLUME, 0, 0);
        case PB_SET_VOLUME:
            if (evaluator->state->suppressAudioFades)
            {
                evaluator->state->fadeVolume = (evaluator->volume > 0 ? evaluator->volume : 0);
                evaluator->phase = evaluator->volume <= 0 ? PB_STOP : PB_FADE_ALARM;
                break;
            }
            return playback_command(evaluator, command, MM_PLAYBACK_SET_VOLUME,
                                    (evaluator->volume > 0 ? evaluator->volume : 0), 0);
        case PB_FADE_ALARM:
            return playback_command(evaluator, command, MM_PLAYBACK_SET_ALARM, MM_PLAYBACK_FADE_TICKS, 0);
        case PB_SCROLL:
            return playback_command(evaluator, command, MM_PLAYBACK_SCROLL_TEXT, 0, 0);
        case PB_SCROLL_ALARM:
        case PB_START_SCROLL_ALARM:
            return playback_command(evaluator, command, MM_PLAYBACK_SET_ALARM, MM_PLAYBACK_SCROLL_TICKS, 0);
        case PB_PRESENT:
            return playback_command(evaluator, command, MM_PLAYBACK_PRESENT_FRAME, 0, 0);
        case PB_DISPLAY:
            return playback_command(evaluator, command, MM_PLAYBACK_DISPLAY_FRAME, 0, 0);
        case PB_QUERY_SCROLLING:
            return playback_command(evaluator, command, MM_PLAYBACK_QUERY_SCROLLING, 0, 0);
        case PB_CLICK_ON:
            return playback_command(evaluator, command, MM_PLAYBACK_SET_CLICK_PAUSE, 0, 1);
        case PB_QUERY_SOUND:
            return playback_command(evaluator, command, MM_PLAYBACK_QUERY_SOUND, 0, 0);
        case PB_SOUND_ALARM:
            return playback_command(evaluator, command, MM_PLAYBACK_SET_ALARM, 0, 0);
        case PB_TIMED_ALARM:
            return playback_command(evaluator, command, MM_PLAYBACK_SET_ALARM, evaluator->state->pauseTicks, 0);
        case PB_CLICK_OFF:
            return playback_command(evaluator, command, MM_PLAYBACK_SET_CLICK_PAUSE, 0, 0);
        case PB_READ_TIME:
            return playback_command(evaluator, command, MM_PLAYBACK_READ_TIME, 0, 0);
        case PB_FRAME_ALARM:
            return playback_command(evaluator, command, MM_PLAYBACK_SET_ALARM, evaluator->delay, 1);
        case PB_READ_POSITION:
            return playback_command(evaluator, command, MM_PLAYBACK_READ_POSITION, 0, 0);
        case PB_SWITCH_SCENE:
            return playback_command(evaluator, command, MM_PLAYBACK_SWITCH_SCENE, evaluator->position.scene + 1,
                                    0);
        case PB_REPLAY_SCENE:
            return playback_command(evaluator, command, MM_PLAYBACK_REPLAY_SCENE, 0, 0);
        case PB_ADVANCE_FRAME:
            return playback_command(evaluator, command, MM_PLAYBACK_ADVANCE_FRAME, evaluator->position.frame + 1,
                                    0);
        case PB_RENDER:
            return playback_command(evaluator, command, MM_PLAYBACK_RENDER_WORLD, 0, 0);
        case PB_DONE:
            command->kind = MM_PLAYBACK_FINISHED;
            command->flag = 0;
            command->value = 0;
            return 1;
        }
    }
}

int mm_playback_complete(MMPlaybackEvaluator *evaluator, const MMPlaybackReply *reply)
{
    if (!evaluator->waiting)
        return 0;
    evaluator->waiting = 0;
    switch (evaluator->phase)
    {
    case PB_STOP:
        evaluator->phase = PB_DONE;
        break;
    case PB_READ_VOLUME:
        evaluator->volume =
            (evaluator->state->suppressAudioFades && evaluator->state->fadeVolume >= 0 ? evaluator->state->fadeVolume
                                                                                       : reply->number) -
            evaluator->state->fadeOriginalVolume / (MM_PLAYBACK_FADE_SECONDS * 4);
        if (evaluator->volume <= 0)
            evaluator->state->stopRequested = 1;
        evaluator->phase = PB_SET_VOLUME;
        break;
    case PB_SET_VOLUME:
        evaluator->state->fadeChangedVolume = 1;
        evaluator->state->fadeVolume = (evaluator->volume > 0 ? evaluator->volume : 0);
        evaluator->phase = evaluator->volume <= 0 ? PB_STOP : PB_FADE_ALARM;
        break;
    case PB_FADE_ALARM:
    case PB_SOUND_ALARM:
        evaluator->phase = reply->success ? PB_DONE : PB_STOP;
        break;
    case PB_SCROLL:
        evaluator->state->scrolling = reply->value;
        evaluator->phase = reply->value ? PB_SCROLL_ALARM : PB_CHECK_PAUSE;
        break;
    case PB_SCROLL_ALARM:
        if (!reply->success)
            evaluator->state->scrolling = 0;
        evaluator->phase = reply->success ? PB_DONE : PB_STOP;
        break;
    case PB_PRESENT:
        evaluator->phase = reply->success ? PB_DISPLAY : PB_STOP;
        break;
    case PB_DISPLAY:
        evaluator->phase = PB_QUERY_SCROLLING;
        break;
    case PB_QUERY_SCROLLING:
        evaluator->phase = reply->value ? PB_START_SCROLL_ALARM : PB_CHECK_PAUSE;
        break;
    case PB_START_SCROLL_ALARM:
        if (reply->success)
            evaluator->state->scrolling = 1;
        evaluator->phase = reply->success ? PB_DONE : PB_STOP;
        break;
    case PB_CLICK_ON:
    case PB_TIMED_ALARM: /* The original timed-pause alarm's return value is ignored. */
        evaluator->phase = PB_DONE;
        break;
    case PB_QUERY_SOUND:
        if (reply->value)
        {
            evaluator->state->pausing = 1;
            evaluator->phase = PB_SOUND_ALARM;
        }
        else
        {
            evaluator->state->pausing = 0;
            evaluator->state->wait = MM_PLAYBACK_WAIT_NONE;
            evaluator->phase = PB_READ_TIME;
        }
        break;
    case PB_CLICK_OFF:
        evaluator->state->pausing = 0;
        evaluator->phase = PB_READ_TIME;
        break;
    case PB_READ_TIME:
        evaluator->delay =
            evaluator->state->unpaced ? 0 : mm_playback_frame_delay(evaluator->frameStart, reply->timestamp);
        evaluator->phase = PB_FRAME_ALARM;
        break;
    case PB_FRAME_ALARM:
        evaluator->phase = reply->success ? PB_READ_POSITION : PB_STOP;
        break;
    case PB_READ_POSITION:
        evaluator->position = reply->position;
        switch (mm_playback_after_frame(&evaluator->position, 0))
        {
        case MM_PLAYBACK_ADVANCE_FRAME:
            evaluator->phase = PB_ADVANCE_FRAME;
            break;
        case MM_PLAYBACK_SWITCH_SCENE:
            evaluator->state->transition = evaluator->position.transition;
            evaluator->phase = PB_SWITCH_SCENE;
            break;
        default:
            evaluator->state->stopRequested = 1;
            evaluator->phase = PB_DONE;
            break;
        }
        break;
    case PB_SWITCH_SCENE:
        evaluator->phase = reply->success ? PB_REPLAY_SCENE : PB_DONE;
        if (!reply->success)
            evaluator->state->stopRequested = 1;
        break;
    case PB_REPLAY_SCENE:
    case PB_ADVANCE_FRAME:
        evaluator->phase = reply->success ? PB_RENDER : PB_DONE;
        if (!reply->success)
            evaluator->state->stopRequested = 1;
        break;
    case PB_RENDER:
        ++evaluator->state->renderedFrames;
        evaluator->phase = PB_DONE;
        break;
    default:
        return 0;
    }
    return 1;
}

int mm_play_next(MMPlayEvaluator *evaluator, MMPlayCommand *command)
{
    if (evaluator->waiting)
        return 0;
    for (;;)
    {
        switch (evaluator->phase)
        {
        case PLAY_TOGGLE:
            if (evaluator->state->playing)
            {
                if (evaluator->state->stopRequested)
                    evaluator->phase = PLAY_DONE;
                else
                {
                    evaluator->state->stopRequested = 1;
                    evaluator->phase = evaluator->state->pausing ? PLAY_RENDER_STOP : PLAY_DISPATCH_SOUNDS;
                }
            }
            else
                evaluator->phase = PLAY_CHECK_SCENE;
            break;
        case PLAY_BEGIN:
            evaluator->state->oldSoundsEnabled = evaluator->state->soundsEnabled;
            evaluator->state->soundsEnabled = 1;
            evaluator->state->renderedFrames = 0;
            evaluator->phase = PLAY_READ_TIME;
            break;
        case PLAY_RENDER_STOP:
            return play_command(evaluator, command, MM_PLAY_RENDER_STOP, 0);
        case PLAY_CHECK_SCENE:
            return play_command(evaluator, command, MM_PLAY_CHECK_SCENE, 0);
        case PLAY_RESET_SOUNDS:
            return play_command(evaluator, command, MM_PLAY_RESET_SOUNDS, 0);
        case PLAY_QUERY_END:
            return play_command(evaluator, command, MM_PLAY_QUERY_END, 0);
        case PLAY_REWIND:
            return play_command(evaluator, command, MM_PLAY_REWIND, 0);
        case PLAY_READ_TIME:
            return play_command(evaluator, command, MM_PLAY_READ_TIME, 0);
        case PLAY_START_CLOCK:
            return play_command(evaluator, command, MM_PLAY_START_CLOCK, 0);
        case PLAY_PREPARE_SCENE:
            return play_command(evaluator, command, MM_PLAY_PREPARE_SCENE, 0);
        case PLAY_NOTIFY_STOPPED:
            return play_command(evaluator, command, MM_PLAY_NOTIFY_STOPPED, 0);
        case PLAY_CLOCK_FAILED:
            return play_command(evaluator, command, MM_PLAY_STARTUP_FAILED, 0);
        case PLAY_PREPARE_FAILED:
            return play_command(evaluator, command, MM_PLAY_STARTUP_FAILED, 1);
        case PLAY_DISABLE_ACCELERATORS:
            return play_command(evaluator, command, MM_PLAY_DISABLE_ACCELERATORS, 0);
        case PLAY_QUERY_OPENING:
            return play_command(evaluator, command, MM_PLAY_QUERY_OPENING, 0);
        case PLAY_REFRESH_OPENING:
            return play_command(evaluator, command, MM_PLAY_REFRESH_OPENING, 0);
        case PLAY_BACKGROUND_SOUND:
            return play_command(evaluator, command, MM_PLAY_BACKGROUND_SOUND, 0);
        case PLAY_DISPATCH_SOUNDS:
            return play_command(evaluator, command, MM_PLAY_DISPATCH_SOUNDS, evaluator->state->soundsEnabled);
        case PLAY_DONE:
            command->kind = MM_PLAY_FINISHED;
            command->flag = 0;
            return 1;
        }
    }
}

int mm_play_complete(MMPlayEvaluator *evaluator, const MMPlaybackReply *reply)
{
    if (!evaluator->waiting)
        return 0;
    evaluator->waiting = 0;
    switch (evaluator->phase)
    {
    case PLAY_RENDER_STOP:
        evaluator->phase = PLAY_DISPATCH_SOUNDS;
        break;
    case PLAY_CHECK_SCENE:
        evaluator->phase = reply->value ? PLAY_RESET_SOUNDS : PLAY_NOTIFY_STOPPED;
        break;
    case PLAY_RESET_SOUNDS:
        evaluator->phase = PLAY_QUERY_END;
        break;
    case PLAY_QUERY_END:
        evaluator->phase = reply->value ? PLAY_REWIND : PLAY_BEGIN;
        break;
    case PLAY_REWIND:
        evaluator->phase = reply->success ? PLAY_BEGIN : PLAY_NOTIFY_STOPPED;
        break;
    case PLAY_READ_TIME:
        evaluator->state->startTimestamp = reply->timestamp;
        evaluator->state->stopRequested = 0;
        evaluator->phase = PLAY_START_CLOCK;
        break;
    case PLAY_START_CLOCK:
        evaluator->phase = reply->success ? PLAY_PREPARE_SCENE : PLAY_CLOCK_FAILED;
        break;
    case PLAY_PREPARE_SCENE:
        evaluator->phase = reply->success ? PLAY_DISABLE_ACCELERATORS : PLAY_PREPARE_FAILED;
        break;
    case PLAY_DISABLE_ACCELERATORS:
        evaluator->phase = PLAY_QUERY_OPENING;
        break;
    case PLAY_QUERY_OPENING:
        evaluator->phase = reply->value ? PLAY_REFRESH_OPENING : PLAY_BACKGROUND_SOUND;
        break;
    case PLAY_REFRESH_OPENING:
        evaluator->phase = PLAY_BACKGROUND_SOUND;
        break;
    case PLAY_BACKGROUND_SOUND:
        evaluator->phase = PLAY_DISPATCH_SOUNDS;
        break;
    case PLAY_NOTIFY_STOPPED:
    case PLAY_CLOCK_FAILED:
    case PLAY_PREPARE_FAILED:
    case PLAY_DISPATCH_SOUNDS:
        evaluator->phase = PLAY_DONE;
        break;
    default:
        return 0;
    }
    return 1;
}

double mm_playback_fps(int rate, int modifier)
{
    double fps;
    if (rate == 4)
        return 0;
    if (rate == 0 && modifier == 0)
        return -1;
    fps = rate == 2 ? 12 : rate == 3 ? 24 : 6;
    return fps * (modifier == 1 ? 1.35805 : modifier == 2 ? 0.918485 : 1.0);
}

void mm_frame_pacer_reset(MMFramePacer *pacer, double rate)
{
    pacer->fps = rate;
    pacer->fraction = 0;
    pacer->pending = 0;
}
void mm_frame_pacer_schedule(MMFramePacer *pacer, MMPlaybackU32 frameStart)
{
    double milliseconds = (pacer->fps > 0 ? 1000.0 / pacer->fps : 0) + pacer->fraction;
    MMPlaybackU32 delay = (MMPlaybackU32)milliseconds;
    pacer->fraction = milliseconds - delay;
    pacer->due = frameStart + delay;
    pacer->pending = 1;
}
int mm_frame_pacer_take_due(MMFramePacer *pacer, MMPlaybackU32 now)
{
    if (!pacer->pending || (MMPlaybackU32)(now - pacer->due) > 0x7fffffffUL)
        return 0;
    pacer->pending = 0;
    return 1;
}
void mm_frame_pacer_pause(MMFramePacer *pacer, MMPlaybackU32 now)
{
    pacer->pausedAt = now;
}
void mm_frame_pacer_resume(MMFramePacer *pacer, MMPlaybackU32 now)
{
    if (pacer->pending)
        pacer->due += now - pacer->pausedAt;
}

void mm_polled_playback_set_rate(MMPolledPlayback *state, double fps)
{
    state->frame_ms = state->pacing_ms = state->carry_ms = 0;
    mm_frame_pacer_reset(&state->pacer, fps);
}
void mm_polled_playback_reset(MMPolledPlayback *state, double fps, int continuing)
{
    memset(state, 0, sizeof(*state));
    mm_polled_playback_set_rate(state, fps);
    state->restore_music = 1;
    state->was_playing = continuing;
}
void mm_polled_playback_begin_transition(MMPolledPlayback *state, int authored_kind)
{
    state->transition = authored_kind;
    state->transition_ms = 0;
    state->was_playing = 1;
}
double mm_polled_playback_frame_seconds(const MMPolledPlayback *state)
{
    if (state->transition)
        return (state->transition_ms - mm_graphics_movie_transition(state->transition).duration_ms) / 1000;
    return state->frame_seconds;
}

static int polled_frame_due(MMPolledPlayback *state, double elapsed)
{
    const double clock_period = 4294967296.0;
    const double legacy_interval = 1000.0 * MM_PLAYBACK_FRAME_TICKS / MM_PLAYBACK_TICKS_PER_SECOND;
    double start, late;
    MMPlaybackU32 now;

    if (state->pacer.fps < 0)
    {
        state->frame_ms += elapsed;
        if (state->frame_ms < legacy_interval)
            return 0;
        /* Carry ordinary host jitter while capping the effect of a stall. */
        state->frame_ms -= legacy_interval;
        if (state->frame_ms > legacy_interval / 2)
            state->frame_ms = legacy_interval / 2;
        return 1;
    }
    /* Keep fractions while wrapping before conversion to the pacer's timestamp. */
    state->pacing_ms += elapsed < clock_period ? elapsed : fmod(elapsed, clock_period);
    if (state->pacing_ms >= clock_period)
        state->pacing_ms -= clock_period;
    now = (MMPlaybackU32)state->pacing_ms;
    if (!state->pacer.pending)
    {
        start = state->pacing_ms - state->carry_ms;
        if (start < 0)
            start += clock_period;
        mm_frame_pacer_schedule(&state->pacer, (MMPlaybackU32)start);
        state->carry_ms = 0;
        return 0;
    }
    if (!mm_frame_pacer_take_due(&state->pacer, now))
        return 0;
    /* Retain small refresh jitter so each deadline need not round up a tick. */
    late = (MMPlaybackU32)(now - state->pacer.due);
    if (state->pacer.fps > 0 && late < 1000.0 / state->pacer.fps)
        state->carry_ms = late;
    return 1;
}

int mm_polled_playback_update(MMPolledPlayback *state, int playing, double elapsed, int click,
                              const MMPolledPlaybackServices *services, MMPolledPlaybackUpdate *update)
{
    MMPolledFrameWait wait = {MM_PLAYBACK_WAIT_NONE, 0};
    MMScrollUpdate scroll;
    int was_scrolling, needs_scroll, pending;

    update->advance = update->scroll_step = 0;
    if (!playing)
    {
        state->was_playing = 0;
        return 1;
    }
    if (!state->was_playing)
        elapsed = 0;
    state->was_playing = 1;
    if (state->transition)
    {
        state->transition_ms += elapsed;
        if (state->transition_ms < mm_graphics_movie_transition(state->transition).duration_ms)
            return 1;
        state->transition = 0;
        services->transition_ended(services->context);
        elapsed = 0;
    }
    if (!state->entered)
    {
        mm_scroll_reset(&state->scroll);
        state->frame_seconds = 0;
        if (!services->enter_frame(services->context, state->restore_music, state->pacer.fps, &wait))
            return 0;
        state->entered = 1;
        state->restore_music = 0;
        state->wait = wait.kind;
        state->wait_ms = wait.milliseconds;
        elapsed = 0;
    }
    was_scrolling = state->scroll.holding;
    if (was_scrolling || state->wait != MM_PLAYBACK_WAIT_CLICK)
        state->frame_seconds += elapsed / 1000;
    if (!services->measure_scroll(services->context, &needs_scroll))
        return 0;
    scroll = mm_scroll_update(&state->scroll, elapsed, needs_scroll);
    update->scroll_step = scroll.step;
    if (scroll.holding)
    {
        state->carry_ms = 0;
        return 1;
    }
    if (was_scrolling)
        elapsed = 0;
    switch (state->wait)
    {
    case MM_PLAYBACK_WAIT_CLICK:
        state->carry_ms = 0;
        if (!click)
            return 1;
        break;
    case MM_PLAYBACK_WAIT_SOUND:
        state->carry_ms = 0;
        if (!services->sound_pending(services->context, &pending))
            return 0;
        if (pending)
            return 1;
        break;
    case MM_PLAYBACK_WAIT_TIME:
        state->carry_ms = 0;
        state->wait_ms -= elapsed;
        if (state->wait_ms > 0)
            return 1;
        break;
    default:
        break;
    }
    if (state->wait >= MM_PLAYBACK_WAIT_CLICK && state->wait <= MM_PLAYBACK_WAIT_TIME)
    {
        /* The interval begins when the wait clears. */
        state->wait = MM_PLAYBACK_WAIT_NONE;
        elapsed = 0;
    }
    if (polled_frame_due(state, elapsed))
    {
        state->entered = 0;
        update->advance = 1;
    }
    return 1;
}
int mm_playback_should_render(MMPlaybackState *state, int tracking)
{
    if (!state->idleSeen && !tracking)
        return 0;
    state->idleSeen = 0;
    return 1;
}
