/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Clock class. Clocks provide timing and alarm functionality. Clocks get
    CPU time by inserting themselves in the command handler list (attached
    to the CommandExecutionManager). This causes Clock::FCmdAll to be called every time a command
    comes through the CommandExecutionManager. The time of a clock is updated only when
    Clock::FCmdAll is called. So the following code will not time the operation:

        dtim = vclok.TimCur();
        for (...)
            {
            ...
            }
        dtim = vlok.TimCur() - dtim;

    At the end of this, dtim will always be zero (unless Clock::FCmdAll is
    somehow called in the loop - in which case dtim still won't be the exact
    timing of the loop). To do this type of timing use TsCurrent() or
    TsCurrentSystem(). Clocks use TsCurrent(), so scaling the application
    time scales all clocks.

    Added feature: TimCur now takes an optional boolean parameter indicating
    whether the time should be calculated or just the current value as
    described above. The default is false (return the current value ...).
    FSetAlarm also has an optional boolean specifying whether the alarm
    time should be computed from the current app time or from the current
    value (ie from TimCur(fTrue) or TimCur(fFalse)). The default is to use
    TimCur(fFalse) (old behavior).

    By default, if a clock's time is computed to exceed its next alarm's
    time, the clock's time "slips" back to the alarm's time. Eg, if an
    alarm is set for tim = 1000 and the app does something that takes a long
    time, so that the next time thru the clock's FCmdAll the clocks time
    is computed to be 1200, the clock's time is set back to 1000 and the
    alarm is triggered. If the clock should not slip in this way, fclokNoSlip
    should be specified in the constructor's grfclok parameter.

    fclokReset means that the clock's time should be reset to 0 every
    time the following commands come through the command queue:
    cidKey, cidTrackMouse, cidMouseMove, any cid less than cidMinNoMenu.
    Such a clock could be used for screen saver functionality or time-out
    animations, etc.

    WARNING: If alarms are not handled and set appropriately, the app can
    sometimes get into a tight loop that the user can't break into.
    Suppose a command handler "A" wants to do something every 1/10 of a second.
    It sets up a clock and sets an alarm for 1/10 of a second from now.
    When the alarm goes off, A sets the next alarm for 1/10 of a second from
    now, does some work that takes 1/2 second (we're running on a slow
    machine), then enqueues a command "cidFoo" to another object "B".
    Assuming the command queue was empty, the next command processed by the
    CommandExecutionManager is cidFoo. This causes the alarm to go off again (remember that alarms
    go off during the FCmdAll call). And the whole process repeats. The
    command queue is never empty in the main app loop, so we never check the
    system event queue, so the user can sit there and hit the keyboard or play
    with the mouse as much as they want and we will never see it. The way to
    avoid this is to set the next alarm after the work is done, and better
    yet, don't reset the alarm until all commands resulting from the alarm
    have been processed. Handler A should do the following: do its work,
    enqueue cidFoo to object B, enqueue cidBar to itself. When it gets cidBar,
    it sets an alarm for 1/10 of a second into the future. This guarantees
    a 1/10 second gap between then end of handling one alarm and starting
    to handle the next alarm.

***************************************************************************/
/* kauai-core consumer (Task 5 of cmd_core split): only needs the
   chunky/util runtime + the cmd_core seam (CommandHandler / CEM) +
   clok's own header. No frame.h -- kauai-core has no UI headers. */
#include <exception>
#include <new>
#include "kauai_core.h"
#include "cmd.h"
#include "clok.h"
#include "document_storage_host.h"
#include "mm/wide.h"
ASSERTNAME

RTCLASS(Clock)

BEGIN_CMD_MAP_BASE(Clock)
END_CMD_MAP(&Clock::FCmdAll, pvNil, kgrfcmmAll)

const LegacyLong kcmhlClok = kswMin; // put clocks at the head of the list
MMAlarmClock *Clock::_clocks;

static_assert(kdtimSecond == MM_CLOCK_TICKS_PER_SECOND, "native alarm clock tick rate changed");
static const MMClockCommandIds kClockCommands = {cidAlarm, cidKey, cidTrackMouse, cidMouseMove, cidMinNoMenu};

struct Clock::Host : DocumentStorageHost
{
    PClock clock;

    // Delivery can destroy the clock before this adapter goes out of scope.
    explicit Host(PClock owner) : DocumentStorageHost(nullptr), clock(owner) {}
    MMClockServices Services() { return {this, Now, Deliver}; }

    static int Now(void *context, MMU32 *stamp)
    {
        auto &host = *static_cast<Host *>(context);
        return host.Guard([&] {
            *stamp = TsCurrent();
            return 1;
        });
    }

    static int Deliver(void *context, MMI32 id, const MMClockAlarm *alarm)
    {
        auto &host = *static_cast<Host *>(context);
        return host.Guard([&] {
            Command cmd;
            ClearPb(&cmd, size(Command));
            cmd.cid = cidAlarm;
            cmd.pcmh = static_cast<PCommandHandler>(alarm->target);
            cmd.rglw[0] = id;
            cmd.rglw[1] = mm_signed_word(alarm->time);
            cmd.rglw[2] = alarm->value;
            if (!cmd.pcmh)
            {
                vpcex->EnqueueCmd(&cmd);
                return 1;
            }

            PClock clock = host.clock;
            clock->AddRef();
            Assert(clock->_references.count > 1, 0);
            cmd.pcmh->FDoCmd(&cmd);
            const bool alive = clock->_references.count != 1;
            clock->Release();
            if (alive)
                AssertPo(clock, 0);
            return alive ? 1 : 0;
        });
    }
};

Clock::Clock(LegacyLong hid, LegacyUlong grfclok) : CommandHandler(hid)
{
#ifdef DEBUG
    const int trash = 1;
#else
    const int trash = 0;
#endif
    const auto allocator = Group::CollectionAllocator();
    AssertDo(mm_alarm_clock_init(&_clock, this, Hid(), grfclok, &allocator, trash), "invalid clock allocator");
    mm_clock_register(&_clocks, &_clock);
    AssertThis(0);
}

Clock::~Clock()
{
    AssertDo(mm_clock_unregister(&_clocks, &_clock), "clok not in linked list");
    DocumentStorageHost host(&_clock.alarms);
    AssertDo(mm_alarm_clock_dispose(&_clock), "invalid clock storage");
    host.Rethrow();
}

PClock Clock::PclokFromHid(LegacyLong hid)
{
    auto found = mm_clock_find(_clocks, hid);
    auto clock = found ? static_cast<PClock>(found->owner) : nullptr;
    AssertNilOrPo(clock, 0);
    return clock;
}

void Clock::BuryCmh(PCommandHandler pcmh)
{
    mm_clock_bury_target(_clocks, pcmh);
}

void Clock::RemoveCmh(PCommandHandler pcmh)
{
    AssertThis(0);
    mm_clock_remove_target(&_clock, pcmh);
}

void Clock::Start(LegacyUlong tim)
{
    AssertThis(0);
    Host host(this);
    const auto services = host.Services();
    mm_alarm_clock_start(&_clock, tim, &services);
    host.Rethrow();
    vpcex->RemoveCmh(this, kcmhlClok);
    vpcex->FAddCmh(this, kcmhlClok, kgrfcmmAll);
}

void Clock::Stop()
{
    AssertThis(0);
    vpcex->RemoveCmh(this, kcmhlClok);
}

LegacyUlong Clock::TimCur(bool fAdjustForDelay)
{
    AssertThis(0);
    const MMU32 stamp = fAdjustForDelay ? TsCurrent() : 0;
    return mm_alarm_clock_time(&_clock, fAdjustForDelay, stamp);
}

bool Clock::FSetAlarm(LegacyLong dtim, PCommandHandler pcmhNotify, LegacyLong lwUser, bool fAdjustForDelay)
{
    AssertThis(0);
    AssertIn(dtim, 0, kcbMax);
    AssertNilOrPo(pcmhNotify, 0);
    const MMU32 stamp = fAdjustForDelay ? TsCurrent() : 0;
    DocumentStorageHost host(&_clock.alarms);
    const int scheduled = mm_alarm_clock_schedule(&_clock, dtim, pcmhNotify, lwUser, fAdjustForDelay, stamp);
    host.Rethrow();
    return scheduled != 0;
}

bool Clock::FCmdAll(PCommand pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    Host host(this);
    const auto services = host.Services();
    mm_alarm_clock_dispatch(&_clock, pcmd->cid, &kClockCommands, &services);
    host.Rethrow();
    return fFalse;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a Clock.
***************************************************************************/
void Clock::AssertValid(LegacyUlong grf)
{
    Clock_PAR::AssertValid(0);
    Assert(_clock.current <= _clock.next_time, "_timNext too small");
    Assert((_clock.flags & fclokNoSlip) || _clock.alarm_delay == 0, "_dtimAlarm should be 0");
}

/***************************************************************************
    Mark memory for the Clock.
***************************************************************************/
void Clock::MarkMem(void)
{
    AssertValid(0);
    Clock_PAR::MarkMem();
    MarkPv(_clock.alarms.buffers[0].data);
}

/***************************************************************************
    Static method to mark all the CLOKs
***************************************************************************/
void Clock::MarkAllCloks(void)
{
    for (auto clock = _clocks; clock; clock = clock->next)
    {
        auto owner = static_cast<PClock>(clock->owner);
        AssertPo(owner, 0);
        MarkMemObj(owner);
    }
}
#endif // DEBUG
