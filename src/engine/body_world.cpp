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

    ThreeMMBodyPick pick;
    three_mm_body_pick_begin(&pick);
    pbwld->IterateActorsInPt(Body::_FFilter, &pick, xp, yp);
    auto services = Body::_GeometryServices();
    ThreeMMI32 set;
    auto body = static_cast<PBody>(three_mm_body_pick_result(&pick, pibset ? &set : nullptr, &services));
    if (body && pibset)
        *pibset = set;
    return body;
}

bool BodyWorld::FPtInBody(Body *body, PWorld world, long xp, long yp, long *pibset)
{
    AssertPo(body, 0);
    AssertPo(world, 0);
    AssertVarMem(pibset);
    three_mm_body_search_begin(&body->_geometry);
    world->IterateActorsInPt(&Body::_FFilterSearch, body, xp, yp);
    *pibset = body->_geometry.set;
    return body->_geometry.found != 0;
}
