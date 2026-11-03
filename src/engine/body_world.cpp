/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include "soc.h"
#include "body_world.h"
ASSERTNAME

void BodyWorld::Bind(Body *body, PWorld world)
{
    AssertPo(body, 0);
    AssertPo(world, 0);
    body->SetRenderCallbacks(world, Attach, Changed);
}

void BodyWorld::Attach(void *context, Body *body)
{
    PWorld world = static_cast<PWorld>(context);
    world->AddActor(body->_PbactRoot());
    world->SetBeginRenderCallback(Body::_PrepareToRender);
    world->SetActorRenderedCallback(Body::_BactRendered);
    world->SetGetRcCallback(Body::_GetRc);
}

void BodyWorld::Changed(void *context)
{
    static_cast<PWorld>(context)->MarkDirty();
}

Body *BodyWorld::PbodyClicked(long xp, long yp, PWorld pbwld, long *pibset)
{
    AssertNilOrVarMem(pibset);
    AssertPo(pbwld, 0);

    PBody pbody;
    long ibact;

    Body::_pbodyClosestClicked = pvNil;
    Body::_dzpClosestClicked = BR_SCALAR_MAX;

    pbwld->IterateActorsInPt(Body::_FFilter, pvNil, xp, yp);

    pbody = Body::_pbodyClosestClicked;
    if (pvNil == Body::_pbodyClosestClicked)
    {
        return pvNil;
    }
    AssertPo(Body::_pbodyClosestClicked, 0);
    if (pvNil != pibset)
    {
        *pibset = ivNil;
        for (ibact = 0; ibact < Body::_pbodyClosestClicked->_cbactPart; ibact++)
        {
            if (Body::_pbactClosestClicked == Body::_pbodyClosestClicked->_PbactPart(ibact))
            {
                *pibset = Body::_pbodyClosestClicked->_Ibset(ibact);
                break;
            }
        }
    }
    return Body::_pbodyClosestClicked;
}

bool BodyWorld::FPtInBody(Body *body, PWorld world, long xp, long yp, long *pibset)
{
    AssertPo(body, 0);
    AssertPo(world, 0);
    AssertVarMem(pibset);
    body->_fFound = fFalse;
    body->_ibset = ivNil;
    world->IterateActorsInPt(&Body::_FFilterSearch, body, xp, yp);
    *pibset = body->_ibset;
    return body->_fFound;
}
