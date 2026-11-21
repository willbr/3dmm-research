/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include "soc.h"
#include "body_world.h"
ASSERTNAME

void BodyWorld::Bind(PBody body, PWorld world)
{
    Assert(body, "missing body");
    AssertPo(world, 0);
    EngineBody::SetRenderCallbacks(body, world, Attach, Changed);
}

void BodyWorld::Attach(void *context, void *body)
{
    PWorld world = static_cast<PWorld>(context);
    world->AddActor(EngineBody::Root(static_cast<PBody>(body)));
    world->SetBodyRenderCallbacks(EngineBody::ReadRenderBody, EngineBody::PrepareRenderBody);
    world->SetActorRenderedCallback(EngineBody::ActorRendered);
    world->SetGetRcCallback(EngineBody::RenderRect);
}

void BodyWorld::Changed(void *context)
{
    static_cast<PWorld>(context)->MarkDirty();
}

struct BodyWorldPoint
{
    PWorld world;
    long x, y;
};

static void ScanBodyWorld(void *context, MMBodyHitVisitor *visit, void *visitorContext)
{
    const auto &point = *static_cast<const BodyWorldPoint *>(context);
    point.world->IterateActorsInPt(visit, visitorContext, point.x, point.y);
}

PBody BodyWorld::PbodyClicked(long xp, long yp, PWorld pbwld, long *pibset)
{
    AssertNilOrVarMem(pibset);
    AssertPo(pbwld, 0);

    BodyWorldPoint point = {pbwld, xp, yp};
    const auto services = EngineBody::GeometryServices();
    MMI32 set;
    auto body = static_cast<PBody>(mm_body_pick_at(ScanBodyWorld, &point, pibset ? &set : nullptr, &services));
    if (body && pibset)
        *pibset = set;
    return body;
}

bool BodyWorld::FPtInBody(PBody body, PWorld world, long xp, long yp, long *pibset)
{
    Assert(body, "missing body");
    AssertPo(world, 0);
    AssertVarMem(pibset);
    BodyWorldPoint point = {world, xp, yp};
    const auto services = EngineBody::GeometryServices();
    MMI32 set;
    const bool found = mm_body_search_at(body, ScanBodyWorld, &point, &set, &services) != 0;
    *pibset = set;
    return found;
}
