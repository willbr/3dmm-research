/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME
using namespace ActorEvent;

bool ActorStepSource::FStep(LegacyLong action, LegacyLong cel, BRS *step) const
{
    AssertVarMem(step);
    *step = rZero;
    return resolve != pvNil && resolve(context, action, cel, step);
}

bool ActorStepSource::FPlaybackStep(const ActorFrameState &state, BRS *step) const
{
    AssertVarMem(step);
    *step = rZero;
    if (state.step == kdwrNil)
    {
        if (!FStep(state.action, state.cel, step))
            return false;
    }
    else
        *step = state.step;
    *step = BrsMul(*step, state.transform.rScaleStep);
    return true;
}

static bool FindEntrance(const ActorTrack &track, LegacyLong first, LegacyLong *found)
{
    for (; first < track.Events()->IvMac(); ++first)
    {
        ActorEvent::Base event;
        track.Events()->GetFixed(first, &event);
        if (event.aet == ActorEvent::aetAdd)
        {
            *found = first;
            return true;
        }
    }
    *found = ivNil;
    return false;
}

bool ActorTimeline::FComputeLifetime(const ActorTrack &track, LegacyLong firstFrame, bool rejoining,
                                     const ActorStepSource &steps, LegacyLong *lastFrame)
{
    return FComputeLifetime(track, firstFrame, rejoining, steps, lastFrame, klwMax);
}

bool ActorTimeline::FComputeLifetime(const ActorTrack &track, LegacyLong firstFrame, bool rejoining,
                                     const ActorStepSource &steps, LegacyLong *lastFrame, LegacyLong maxFrames)
{
    AssertVarMem(lastFrame);

    LegacyLong iaev;
    LegacyLong iaevAdd;
    BRS dwr;
    Base aev;
    Base *paev;
    RouteLocation rtel;
    bool fFreezeThisCel;
    LegacyLong anid = 0;
    LegacyLong celn = 0;
    BRS rScale = rOne;
    BRS dwrStep = rZero;
    bool fEndSubRoute = fFalse;  // moving past end of subroute (see Note above)
    bool fDoneSubRoute = fFalse; // finished processing events on the current subroute

    // Locate final subroute
    rtel.irpt = 0;
    rtel.dwrOffset = rZero;
    rtel.dnfrm = -1;
    iaevAdd = -1;
    // REVIEW *****(SeanSe): Why not start at the end and go backwards?
    for (iaev = 0; iaev < track.Events()->IvMac(); iaev++)
    {
        paev = (Base *)track.Events()->QvFixedGet(iaev);
        if (aetAdd == paev->aet)
            iaevAdd = iaev;
    }

    if (iaevAdd < 0)
    {
        (*lastFrame) = firstFrame;
        return fTrue;
    }

    // Loop through each frame
    LegacyLong fFrozen = fFalse;
    LegacyLong iaevNew = 0;
    for ((*lastFrame) = firstFrame; ((rtel.irpt != track.Route()->IvMac()) || (iaevNew != track.Events()->IvMac()));
         (*lastFrame)++)
    {
        if (int64_t(*lastFrame) - firstFrame > maxFrames || *lastFrame == klwMax)
            return fFalse;
        fFreezeThisCel = fFrozen;
        // Find distance to move
        // An aetActn event later in this same frame can modify this.
        if (kdwrNil == dwrStep)
        {
            if (!steps.FStep(anid, celn, &dwr))
            {
                return fFalse;
            }
        }
        else
        {
            dwr = dwrStep;
        }
        dwr = BrsMul(dwr, rScale);
        ActorRouteEvaluator::Advance(track, dwr, &rtel, iaevNew, (*lastFrame), firstFrame, !rejoining, &fEndSubRoute);

        // Scan all events for this frame
        for (iaev = iaevNew; iaev < track.Events()->IvMac(); iaev++)
        {
            track.Events()->GetFixed(iaev, &aev);

            if ((aev.rtel > rtel) && ((aetAdd != aev.aet) || (!fEndSubRoute && !fDoneSubRoute)))
            {
                // To push out subroutes, do not process add events until finished
                // with the previous subroute
                goto LEndFrame;
            }

            switch (aev.aet)
            {
            case aetAdd:
                fFreezeThisCel = fTrue;
                anid = celn = 0;
                if (aev.nfrm > (*lastFrame))
                {
                    goto LEndFrame;
                }
                else
                {
                    rtel = aev.rtel;
                    fDoneSubRoute = fEndSubRoute = fFalse;
                }
                break;

            case aetTweak:
            case aetSnd:
            case aetRem:
            case aetCost:
            case aetRotF:
            case aetRotH:
            case aetPull:
            case aetMove:
                break;

            case aetFreeze:
                int32_t ffriz;
                track.Events()->Get(iaev, &ffriz);
                fFrozen = FPure(ffriz);
                break;

            case aetSize: // Uniform size transformation
                // Adjust step size : affects actor lifetime
                track.Events()->Get(iaev, &rScale);
                break;

            case aetActn:
                Action aevactn;

                track.Events()->Get(iaev, &aevactn);
                anid = aevactn.anid;
                celn = aevactn.celn;
                fFrozen = fFalse;
                fFreezeThisCel = fTrue;
                break;

            case aetStep:
                track.Events()->Get(iaev, &dwrStep);
                break;

            default:
                Assert(0, "Unknown event type");
                break;
            }

            if (aev.rtel > rtel)
                goto LEndFrame;

            if (aev.nfrm != (*lastFrame))
            {
                aev.nfrm = (*lastFrame);
                track.Events()->PutFixed(iaev, &aev);
            }
        }

    LEndFrame:

        // Stepsize and lifetime is a function of celn's
        if (!fFreezeThisCel && !(fEndSubRoute && ActorFrameEvaluator::FSubrouteEventsDone(track, iaev, rtel)))
        {
            celn++;
        }

        // Check for end of subroute or stalled actor (eg breathe in place forever)
#ifdef BUG1960
        // We are not finished updating events in the current subpath until we are either
        // - beyond the subroute (fEndSubRoute)
        // - out of events
        // - stopped with no way to reach more events in the subpath (ie, stalled)
        //   and finished updating later events at this same path location
        if (fEndSubRoute ||
            (dwrStep == rZero &&
             (iaev == track.Events()->IvMac() || (!(aev.rtel.irpt == rtel.irpt && aev.rtel.dwrOffset == rtel.dwrOffset) &&
                                                FIsStalled(track, iaevNew, &rtel)))))
#else  //! BUG1960
        if (fEndSubRoute ||
            ((dwrStep == rZero) && (iaev == track.Events()->IvMac() || FIsStalled(track, iaevNew, &rtel))))
#endif //! BUG1960
        {
            fDoneSubRoute = fTrue;

            // Are there more subroutes?
            if (!FindEntrance(track, iaev, &iaev))
            {
                // For non-static motions, fEndSubRoute is set once the actor is
                // beyond the end of the subroute.  Adjust back one frame.
                if (fEndSubRoute && (dwrStep != rZero) && ((*lastFrame) > firstFrame))
                    (*lastFrame)--;

                return fTrue;
            }

            // Add events jump in space.  Update rtel
            track.Events()->GetFixed(iaev, &aev);
            rtel = aev.rtel;

            // Initialization for _AdvanceRtel()
            anid = celn = 0;
            rtel.dnfrm--;
        }

        iaevNew = iaev;
    }

    Assert(0, "Logic error");
    return fFalse;
}

bool ActorTimeline::FIsStalled(const ActorTrack &track, LegacyLong iaevFirst, RouteLocation *prtel, LegacyLong *piaevLast)
{
    AssertIn(iaevFirst, 0, track.Events()->IvMac());
    AssertNilOrVarMem(piaevLast);

    Base aev;
    LegacyLong iaev;

    for (iaev = iaevFirst; iaev < track.Events()->IvMac(); iaev++)
    {
        track.Events()->GetFixed(iaev, &aev);
        if ((aev.rtel.irpt > prtel->irpt) || (aev.rtel.dwrOffset > prtel->dwrOffset))
        {
            if (piaevLast != pvNil)
            {
                *piaevLast = iaev;
            }
            return fTrue;
        }

        if ((aev.rtel.irpt == prtel->irpt) && (aev.rtel.dwrOffset == prtel->dwrOffset))
        {
            if (aetStep == aev.aet)
            {
                BRS dwrAev;
                track.Events()->Get(iaev, &dwrAev);
                if (rZero != dwrAev)
                    return fFalse;
            }
        }
    }
    return fFalse;
}

bool ActorTimeline::FStepBack(ActorFrameState &state, const ActorTrack &track, const ActorStepSource &steps,
                              RouteLocation *prtel, bool fUpdateStateVar)
{
    AssertVarMem(prtel);
    Assert(fUpdateStateVar || prtel != &state.location, "Probe output aliases restored state");

    LegacyLong celnSav = state.cel;
    LegacyLong nfrmSav = state.frame;
    RouteLocation rtelSav = state.location;
    Base *paev;
    BRS dwrStep;
    RouteLocation rtelAdd;
    BRS dwrT;

    paev = (Base *)track.Events()->QvFixedGet(state.addEvent);
    rtelAdd = paev->rtel;

    if (!state.frozen)
        state.cel--;
    if (rZero == state.step || (rtelAdd.irpt == state.location.irpt && rtelAdd.dwrOffset == state.location.dwrOffset))
    {
        state.frame--;
        state.location.dnfrm--;
        goto LEnd;
    }

    // Set the location to display this actor
    if (!steps.FPlaybackStep(state, &dwrStep))
        goto LFail;

    dwrT = BrsSub(state.location.dwrOffset, dwrStep);

    if (dwrT >= rZero)
        state.location.dwrOffset = dwrT;
    else
    {
        RouteDistancePoint rpt;
        while (dwrT < rZero)
        {
            if (state.location.irpt <= 0)
            {
                Bug("Corrupted event list");
                state.location.irpt = 0;
                dwrT = rZero;
                break;
            }
            state.location.irpt--;
            track.Route()->Get(state.location.irpt, &rpt);
            dwrT = BrsAdd(rpt.dwr, dwrT);
        }
        state.location.dwrOffset = dwrT;
    }
    state.frame--;

LEnd:
    *prtel = state.location;
    if (!fUpdateStateVar)
    {
        state.cel = celnSav;
        state.frame = nfrmSav;
        state.location = rtelSav;
    }
    return fTrue;

LFail:
    state.cel = celnSav;
    state.frame = nfrmSav;
    state.location = rtelSav;
    return fFalse;
}

ActorQuickSeekResult ActorTimeline::QuickBackup(ActorFrameState &state, const ActorTrack &track, LegacyLong lastFrame,
                                                LegacyLong nfrm, const ActorStepSource &steps)
{
    Assert(nfrm < state.frame, "Illegal call to _FBackupToFrm");

    LegacyLong ifrm;
    RouteLocation rtelT;
    RoutePoint xyzOld = state.position;
    RoutePoint xyzT;
    LegacyLong dnfrm = state.frame - nfrm;

#ifdef DEBUG
    Base *paev;
    paev = (Base *)track.Events()->QvFixedGet(state.nextEvent - 1);
    Assert(paev->nfrm < nfrm, "Invalid Call to _FQuickBackupToFrm()");
#endif // DEBUG

    // Don't try to back up during frames beyond the actor's lifetime
    if (nfrm >= lastFrame)
    {
        // GotoFrame() must always exit with state.frame being valid
        // Otherwise, edits will be located at incorrect frames
        state.frame = nfrm;
#ifdef BUG1906
        state.location.dnfrm -= dnfrm;
#else  //! BUG1906
        state.location.dnfrm--;
#endif //! BUG1906
        return {true, true, false};
    }

    // There are no events between here and the destination frame
    // state.firstEventInFrame need not change

    // Walk to the destination location
    // Set the cel of the action
    for (ifrm = state.frame - 1; ifrm >= nfrm; ifrm--)
    {
        if (!FStepBack(state, track, steps, &state.location, true))
            goto LFail;
    }

    ActorRouteEvaluator::Position(track, &state.location, &state.position);
    state.frame = nfrm;

    if (dnfrm > 1 || xyzOld.dxr != state.position.dxr || xyzOld.dzr != state.position.dzr)
    {
        // Check for transitions to vertical motion
        if (!FStepBack(state, track, steps, &rtelT, false))
            goto LFail;

        ActorRouteEvaluator::Position(track, &rtelT, &xyzT);

        if (state.position.dxr == xyzT.dxr && state.position.dzr == xyzT.dzr)
        {
            // Vertical motion next	(backing up)
            // -> Require state.useCurrentRotation == fTrue, but bmat34Cur is not
            // yet computed.
            // -> Quick backup insufficient.
            return {true, false, false};
        }
    }

    return {true, true, true};

LFail:
    return {};
}
