/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME
using namespace ActorEvent;

void ActorRouteEvaluator::Advance(const ActorTrack &track, BRS dwrStep, RouteLocation *prtel, LegacyLong iaevCur,
                                  LegacyLong nfrmCur, LegacyLong nfrmFirst, bool clampAtEvents, bool *pfEndRoute)
{
    AssertPo(track.route, 0);
    AssertPo(track.events, 0);
    AssertVarMem(const_cast<RouteLocation *>(prtel));
    AssertNilOrVarMem(pfEndRoute);

    BRS dwrT;
    bool fEndRoute = fFalse;
    LegacyLong iaev;
    Base aev;

    // If at start of path
    // Note: nfrmFirst is independent with respect to the current frame
    if (nfrmCur <= nfrmFirst)
    {
        prtel->irpt = 0;
        prtel->dwrOffset = rZero;
        prtel->dnfrm = 0;
        goto LDone;
    }

    // End of route means trying to move beyond the last point.
    if (rZero == dwrStep) // Not an "end-of-route"
    {
        prtel->dnfrm++;
        goto LDone;
    }

    // Move to the correct path segment
    dwrT = ((RouteDistancePoint *)track.route->QvGet(prtel->irpt))->dwr;
    dwrT = BrsSub(dwrT, prtel->dwrOffset);
    if (rZero == dwrT)
    {
        Assert(prtel->dwrOffset == rZero, "Expected zero offset");
        prtel->dnfrm++;
        fEndRoute = fTrue;
        goto LDone;
    }

    while (dwrT <= dwrStep && rZero < dwrStep)
    {
        // Spec: Move the partial step
        // Not an "end-of-route" - didn't try to move beyond
        if (rZero == dwrT)
        {
            goto LDoneMove;
        }
        prtel->irpt++;
        AssertIn(prtel->irpt, 1, track.route->IvMac());
        prtel->dwrOffset = rZero;
        dwrStep = BrsSub(dwrStep, dwrT);
        dwrT = ((RouteDistancePoint *)track.route->QvGet(prtel->irpt))->dwr;
    }

    dwrT = ((RouteDistancePoint *)track.route->QvGet(prtel->irpt))->dwr;
    prtel->dwrOffset = BrsAdd(prtel->dwrOffset, dwrStep);

LDoneMove:
    prtel->dnfrm = 0;

    if (clampAtEvents)
    {
        // Spec: Ordinarily, the actor will display at the end of this step.
        // If Actn or Step events exist, the the actor is to display
        // at the location of the event.
        for (iaev = iaevCur; iaev < track.events->IvMac(); iaev++)
        {
            track.events->GetFixed(iaev, &aev);
            if ((aev.rtel <= *prtel) && (aev.aet == aetStep || aev.aet == aetActn))
            {
                *prtel = aev.rtel;
                break;
            }
            if (aev.rtel > *prtel)
                break;
        }
    }

LDone:
    if (pvNil != pfEndRoute)
        *pfEndRoute = fEndRoute;

    return;
}

void ActorRouteEvaluator::Position(const ActorTrack &track, const RouteLocation *prtel, PRoutePoint pxyz)
{
    AssertPo(track.route, 0);
    AssertVarMem(const_cast<RouteLocation *>(prtel));
    AssertVarMem(pxyz);

    RouteDistancePoint rpt;
    RouteDistancePoint rptFirst;
    RouteDistancePoint rptSecond;
    BRS rFract;

    if (rZero == prtel->dwrOffset || (prtel->irpt) + 1 == track.route->IvMac())
    {
        track.route->Get(prtel->irpt, &rpt);
        *pxyz = rpt.xyz;
        return;
    }

    track.route->Get(prtel->irpt, &rptFirst);
    track.route->Get(1 + prtel->irpt, &rptSecond);
    Assert(rptFirst.dwr >= prtel->dwrOffset, "Possible offset bug in _GetXyzFromRtel");

    rFract = BrsDiv(prtel->dwrOffset, rptFirst.dwr);
    Interpolate(&rptFirst.xyz, &rptSecond.xyz, rFract, pxyz);
}

void ActorRouteEvaluator::Interpolate(const RoutePoint *pxyzFirst, const RoutePoint *pxyzSecond, BRS rFract,
                                      PRoutePoint pxyz)
{
    AssertVarMem(const_cast<RoutePoint *>(pxyzFirst));
    AssertVarMem(const_cast<RoutePoint *>(pxyzSecond));
    AssertVarMem(pxyz);

    RoutePoint result;
    // New pt = first + (second - first) * fractoffset;
    result.dxr = BrsSub(pxyzSecond->dxr, pxyzFirst->dxr);
    result.dyr = BrsSub(pxyzSecond->dyr, pxyzFirst->dyr);
    result.dzr = BrsSub(pxyzSecond->dzr, pxyzFirst->dzr);

    result.dxr = BrsMul(rFract, result.dxr);
    result.dyr = BrsMul(rFract, result.dyr);
    result.dzr = BrsMul(rFract, result.dzr);

    result.dxr = BrsAdd(result.dxr, pxyzFirst->dxr);
    result.dyr = BrsAdd(result.dyr, pxyzFirst->dyr);
    result.dzr = BrsAdd(result.dzr, pxyzFirst->dzr);
    *pxyz = result;
}
