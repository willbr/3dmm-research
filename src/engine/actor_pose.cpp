/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "soc_core.h"
ASSERTNAME
using namespace ActorEvent;

void ActorPoseEvaluator::Update(ActorFrameState &state, const ActorTrack &track, const ActorPoseInputs &inputs,
                                const RoutePoint *pxyz, BMAT34 *pbmat34)
{
    AssertVarMem(const_cast<RoutePoint *>(pxyz));
    AssertVarMem(pbmat34);

    RouteDistancePoint rpt;
    BMAT34 bmat34TS; // Scaling matrix
    BMAT34 bmat34TR; // Rotation matrix
    bool fStretchSize;
    Base *paev;

    state.position = *pxyz;
    fStretchSize = (rOne != state.transform.aevpull.rScaleX || rOne != state.transform.aevpull.rScaleY ||
                    rOne != state.transform.aevpull.rScaleZ || rOne != state.transform.rScaleStep);

    // Set the orientation & rotation to zero-change
    BrMatrix34Identity(&bmat34TS);

    // Post apply the rest (face the camera) orientation
    BrMatrix34PostRotateX(&bmat34TS, inputs.restX);
    BrMatrix34PostRotateY(&bmat34TS, inputs.restY);
    BrMatrix34PostRotateZ(&bmat34TS, inputs.restZ);

    if (fStretchSize)
    {
        // Apply any current stretching/squashing
        // This must be applied BEFORE rotation to avoid stretching
        // the actor along skewed axes
        BrMatrix34PostScale(&bmat34TS, state.transform.aevpull.rScaleX, state.transform.aevpull.rScaleY,
                            state.transform.aevpull.rScaleZ);

        // Apply any current uniform sizing
        if (state.transform.rScaleStep != rOne)
        {
            BrMatrix34PostScale(&bmat34TS, state.transform.rScaleStep, state.transform.rScaleStep,
                                state.transform.rScaleStep);
        }
    }

    // bmat34Cur is the all inclusive orientation matrix
    if (state.useCurrentRotation)
    {
        // Single frame rotate events or static segments
        BrMatrix34Mul(pbmat34, &bmat34TS, &state.transform.bmat34Cur); // A = B * C
    }
    else
    {
        // Forward-rotate events or non-static segments
        BrMatrix34Copy(&bmat34TR, &state.transform.bmat34Fwd); // copy to bmat34TR

        // Post apply the path orientation
        AssertIn(state.addEvent, 0, track.Events()->IvMac());
        paev = (Base *)track.Events()->QvFixedGet(state.addEvent);
        track.Route()->Get(paev->rtel.irpt, &rpt);
#ifdef BUG1899
        if (inputs.fixedPathOrientation ||
            (state.location.irpt == paev->rtel.irpt && state.location.dwrOffset == rZero))
#else  //! BUG1899
        if (inputs.fixedPathOrientation || paev->nfrm == state.frame || rpt.dwr == rZero)
#endif //! BUG1899
        {
            // Single point	subroute ->
            // Post apply single point orientation to event rotations
            Add aevadd;
            track.Events()->Get(state.addEvent, &aevadd);
            BrMatrix34PostRotateX(&bmat34TR, aevadd.xa);
            BrMatrix34PostRotateY(&bmat34TR, aevadd.ya);
            BrMatrix34PostRotateZ(&bmat34TR, aevadd.za);
            // Save the path part of the orientation
            state.transform.xaPath = aevadd.xa;
            state.transform.yaPath = aevadd.ya;
            state.transform.zaPath = aevadd.za;
        }
        else
        {
            // Orient along the route
            RouteOrientation(state, track, &bmat34TR, &state.transform.xaPath, &state.transform.yaPath,
                             &state.transform.zaPath);
        }

        // Now combine rotation with stretching
        BrMatrix34Mul(pbmat34, &bmat34TS, &bmat34TR);

        // Note: The result of the entire rotation (including path) needs to
        // be saved - otherwise, the actor will jump when the user first tries
        // to do a tweak-rotate edit.
        BrMatrix34Copy(&state.transform.bmat34Cur, &bmat34TR); // Save final all-included rotation matrix
    }
}

void ActorPoseEvaluator::RouteOrientation(const ActorFrameState &state, const ActorTrack &track, BMAT34 *pbmat34,
                                          BRA *pxa, BRA *pya, BRA *pza, LegacyUlong *pgrfbra)
{
    AssertNilOrVarMem(pbmat34);
    AssertNilOrVarMem(pxa);
    AssertNilOrVarMem(pya);
    AssertNilOrVarMem(pza);

    if (pgrfbra != pvNil)
        *pgrfbra = 0;

    LegacyLong irptPrev;
    LegacyLong irptAdd;
    RoutePoint xyz;
    Base aev;
    RouteDistancePoint rpt;

    if (pvNil != pxa)
        *pxa = aZero;
    if (pvNil != pya)
        *pya = aZero;
    if (pvNil != pza)
        *pza = aZero;

    AssertIn(state.addEvent, 0, track.Events()->IvMac());
    track.Events()->GetFixed(state.addEvent, &aev);
    irptAdd = aev.rtel.irpt;
    irptPrev = (rZero < state.location.dwrOffset || state.location.irpt == aev.rtel.irpt) ? state.location.irpt
                                                                                          : state.location.irpt - 1;

    Assert(rZero == state.location.dwrOffset || irptPrev < track.Route()->IvMac(), "Incorrect offset in path");

    // If at end of subpath, or if on a static segment,
    // retain the orientation the actor last had
    track.Route()->Get(irptPrev, &rpt);
    if (rZero == rpt.dwr)
        irptPrev--;

    if (irptPrev < irptAdd)
        return; // Single point path

    //
    // Compute vector xyz as a weighted average of 3 vectors
    //
    xyz.dxr = rZero;
    xyz.dyr = rZero;
    xyz.dzr = rZero;

    AccumulateTangent(track, &xyz, irptPrev, rFour);

    if (irptPrev > irptAdd)
    {
        AccumulateTangent(track, &xyz, irptPrev - 1, rTwo);
        if (irptPrev - 1 > irptAdd)
        {
            AccumulateTangent(track, &xyz, irptPrev - 2, rOne);
        }
    }

    //
    // Apply the rotation determined by vector xyz
    //
    ApplyVectorRotation(state.actionFlags, &xyz, pbmat34, pxa, pya, pza, pgrfbra);
    return;
}

void ActorPoseEvaluator::AccumulateTangent(const ActorTrack &track, RoutePoint *pxyz, LegacyLong irpt, BRS rw)
{

    RouteDistancePoint rpt1;
    RouteDistancePoint rpt2;
    BRS dwr, dxr, dyr, dzr;

    Assert(irpt + 1 < track.Route()->IvMac(), "irpt out of range");

    track.Route()->Get(irpt, &rpt1);
    track.Route()->Get(irpt + 1, &rpt2);

    dxr = BrsSub(rpt2.xyz.dxr, rpt1.xyz.dxr);
    dyr = BrsSub(rpt2.xyz.dyr, rpt1.xyz.dyr);
    dzr = BrsSub(rpt2.xyz.dzr, rpt1.xyz.dzr);

    //
    // Normalize the vector	(norm ~ max + 1/2 min)
    //
    dwr = BrsAbsMax3(dxr, dyr, dzr);
    if (dxr == dwr)
        dwr = BrsAdd(dwr, (BRS)(LwMax(LwAbs((LegacyLong)dyr), LwAbs((LegacyLong)dzr)) >> 1));
    else
    {
        if (dyr == dwr)
            dwr = BrsAdd(dwr, (BRS)(LwMax(LwAbs((LegacyLong)dxr), LwAbs((LegacyLong)dzr)) >> 1));
        else
            dwr = BrsAdd(dwr, (BRS)(LwMax(LwAbs((LegacyLong)dxr), LwAbs((LegacyLong)dyr)) >> 1));
    }
    dxr = BrsDiv(dxr, dwr);
    dyr = BrsDiv(dyr, dwr);
    dzr = BrsDiv(dzr, dwr);

    dxr = LwBound(dxr, -rOne, rOne);
    dyr = LwBound(dyr, -rOne, rOne);
    dzr = LwBound(dzr, -rOne, rOne);

    //
    // Apply the weight
    //
    if (rw != rOne)
    {
        dxr = BrsMul(rw, dxr);
        dyr = BrsMul(rw, dyr);
        dzr = BrsMul(rw, dzr);
    }

    pxyz->dxr = BrsAdd(pxyz->dxr, dxr);
    pxyz->dyr = BrsAdd(pxyz->dyr, dyr);
    pxyz->dzr = BrsAdd(pxyz->dzr, dzr);
}

void ActorPoseEvaluator::ApplyVectorRotation(LegacyUlong actionFlags, RoutePoint *pxyz, BMAT34 *pbmat34, BRA *pxa, BRA *pya,
                                             BRA *pza, LegacyUlong *pgrfbra)
{
    AssertVarMem(pxyz);
    AssertNilOrVarMem(pbmat34);
    AssertNilOrVarMem(pxa);
    AssertNilOrVarMem(pya);
    AssertNilOrVarMem(pza);
    BRA yaw = aZero;
    BRA pitch = aZero;
    if (pgrfbra != pvNil)
        *pgrfbra = 0;

    // Pitch needs both horizontal components, including when only X rotation
    // is enabled. Normalize all components before converting to fractions.
    BRS dwr = BrsAdd(BrsAbsMax3(pxyz->dxr, pxyz->dyr, pxyz->dzr), rOne);
    BRS rX = BrsDiv(pxyz->dxr, dwr);
    BRS rY = BrsDiv(pxyz->dyr, dwr);
    BRS rZ = BrsDiv(pxyz->dzr, dwr);
    BRFR eX = BrScalarToFraction(rX);
    BRFR eY = BrScalarToFraction(rY);
    BRFR eZ = BrScalarToFraction(rZ);
    if ((actionFlags & factnRotateY) && (pxyz->dxr != rZero || pxyz->dzr != rZero))
    {
        yaw = BR_ATAN2(eX, eZ);
        if (pbmat34 != pvNil)
            BrMatrix34PostRotateY(pbmat34, yaw);
        if (pgrfbra != pvNil)
            *pgrfbra |= factnRotateY;
    }
    if ((actionFlags & factnRotateX) && pxyz->dyr != rZero)
    {
        BRFR horizontal = BrScalarToFraction(BrsAdd(BrsAbs(rX), BrsAbs(rZ)));
        pitch = BR_ATAN2(-eY, horizontal);
        if (pbmat34 != pvNil)
            BrMatrix34PreRotateX(pbmat34, pitch);
        if (pgrfbra != pvNil)
            *pgrfbra |= factnRotateX;
    }
    if (pxa != pvNil)
        *pxa = pitch;
    if (pya != pvNil)
        *pya = yaw;
    if (pza != pvNil)
        *pza = aZero;
}
