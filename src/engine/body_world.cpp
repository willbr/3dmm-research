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
    world->SetBodyRenderCallbacks(Body::_ReadRenderBody, Body::_PrepareRenderBody);
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

    struct Pick
    {
        MMBodyPick state;
        MMBodyGeometryServices services;
    } pick = {{}, Body::_GeometryServices()};
    mm_body_pick_begin(&pick.state);
    pbwld->IterateActorsInPt([](void *context, void *actor, MMI32 depth) {
        auto &pick = *static_cast<Pick *>(context);
        return mm_body_pick_visit(&pick.state, actor, depth, &pick.services);
    }, &pick, xp, yp);
    MMI32 set;
    auto body = static_cast<PBody>(mm_body_pick_result(&pick.state, pibset ? &set : nullptr, &pick.services));
    if (body && pibset)
        *pibset = set;
    return body;
}

bool BodyWorld::FPtInBody(Body *body, PWorld world, long xp, long yp, long *pibset)
{
    AssertPo(body, 0);
    AssertPo(world, 0);
    AssertVarMem(pibset);
    mm_body_search_begin(&body->_geometry);
    struct Search
    {
        Body *body;
        MMBodyGeometryServices services;
    } search = {body, Body::_GeometryServices()};
    world->IterateActorsInPt([](void *context, void *actor, MMI32) {
        auto &search = *static_cast<Search *>(context);
        return mm_body_search_visit(search.body, actor, &search.services);
    }, &search, xp, yp);
    *pibset = body->_geometry.set;
    return body->_geometry.found != 0;
}
