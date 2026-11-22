/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    body.cpp: Body class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    A Body holds the BRender-related data structures that make up what
    Socrates calls an actor.  The Body keeps track of all the body parts'
    models, matrices, and materials that make up the actor's shape,
    position, orientation, and costume.  Actor and Template are the main
    clients of Body.  From the client's point of view, a Body consists
    not of a tree of body parts, but an array of parts and part sets.  The
    "ibact"s and "ibset"s in the Body APIs are indices into these arrays.

    PbodyNew() takes a parameter called pglibactPar, which is a DynamicArray of
    shorts.  Each short is the body part number of a body part's parent
    body part.	For example, suppose you passed in a pglibactPar of:

    (ivNil, 0, 0, 1, 2, 2)

    Body part 0 would have no parent (the first number in the DynamicArray is
    always ivNil).  Body part 1's parent would be body part 0.  Body part
    2's	parent would also be body part 0.  Body part 3's parent would be
    body part 1, etc.  The resulting tree would be:

                0
                |
             +--+--+
             |	   |
             1   +-2-+
             |	 |   |
             3	 4	 5

    Body needs to keep track of three sets of BACTs: first, a root BACT
    whose transformation is changed to position and orient the Body.
    Second, a BACT for the actor hilighting.  Finally, there are
    the body parts for the Body.  So the BRender tree *really* looks like:

                            root
                              |
                       +------+-----+
                       |			|
                     part0		 hilite
                       |
               +-------+--------+
               |		        |
             part1            part2
               |	            |
               |		    +---+---+
               |		    |	    |
             part3	      part4	  part5


    All these BACTs are allocated and released in one actor array.  The
    root is Root(body)[0].  The hilite BACT is Root(body)[1].  The real
    body parts start at Root(body)[2], with State(body).shape.part_count entries.
    There are APIs to access various members of this array more easily.

    The second parameter to PbodyNew is pglibset, which divides the body
    parts into "body part sets."  A body part set is one or more body
    parts which are texture-mapped as a group.  For example, putting
    a shirt on a human would affect the torso and both arms, so those
    three body parts would be grouped into a body part set.  If body
    parts 0, 1, and 2 were in set 0; part 3 was in set 1; and parts 4
    and 5 were in set 2, the set list would be {0, 0, 0, 1, 2, 2}.

    When applying a Material_MTRL to a body part set, the same Material_MTRL is
    attached to each body part in the set.  When a CustomMaterial_CMTL is applied,
    the CustomMaterial_CMTL has a different Material_MTRL per body part in the set.

    Part bindings retain their models and generic materials. A set using a
    custom costume retains the costume, which supplies the part materials.
    BRender actors borrow the resulting draw handles.

***************************************************************************/
#include "soc_core.h"
#include "body_renderer.h"
#include "engine_part_indices.h"
#include <cstring>
ASSERTNAME

#ifdef DEBUG
#define BODY_VALIDATE(body, flags) EngineBody::Validate(body, flags)
#else
#define BODY_VALIDATE(body, flags) ((void)0)
#endif

static const MMBodyRuntimeServices &RuntimeServices()
{
    return mm_kauai_body_renderer()->edit.runtime;
}

static const MMBodyEditServices &EditServices()
{
    return mm_kauai_body_renderer()->edit;
}

void EngineBody::ReleaseSharedMaterial(void)
{
    mm_brender_body_release_highlight();
}

/***************************************************************************
    Set the optional renderer binding while this body is hidden.
***************************************************************************/
void EngineBody::SetRenderCallbacks(PBody body, void *context, void (*attach)(void *, void *), void (*changed)(void *))
{
    Assert(State(body).shape.hidden > 0, "Cannot rebind a visible body");
    State(body).render = {context, attach, changed};
}

/***************************************************************************
    Builds a tree of BACTs for the Body.
***************************************************************************/
PBody EngineBody::Create(PDynamicArray pglibactPar, PDynamicArray pglibset)
{
    return Create(EnginePartIndices::Borrow(pglibactPar), EnginePartIndices::Borrow(pglibset));
}

PBody EngineBody::Create(const MMPartIndexView &parents, const MMPartIndexView &sets)
{
    auto body = mm_brender_body_create(mm_kauai_body_renderer(), &parents, &sets);
#ifdef DEBUG
    Validate(body, fobjAssertFull);
#endif
    return body;
}

/***************************************************************************
    Change the body part hierarchy and/or the body part sets of this Body.
    Models, materials, and matrices are not changed for body parts that
    exist in both the old and reshaped Bodys.
***************************************************************************/
bool EngineBody::ChangeShape(PBody body, PDynamicArray pglibactPar, PDynamicArray pglibset)
{
    return ChangeShape(body, EnginePartIndices::Borrow(pglibactPar), EnginePartIndices::Borrow(pglibset));
}

bool EngineBody::ChangeShape(PBody body, const MMPartIndexView &parents, const MMPartIndexView &sets)
{
    BODY_VALIDATE(body, fobjAssertFull);
    const auto &services = EditServices();
    const bool ok = mm_body_change_shape(body, &parents, &sets, &services) != 0;
    BODY_VALIDATE(body, fobjAssertFull);
    return ok;
}

/***************************************************************************
    Returns the Body that owns the given BACT.  Also, if pibset is not
    nil, returns what body part set this BACT is in.  If the pbact is
    the hilite BACT, pibset is set to ivNil.
***************************************************************************/
PBody EngineBody::FromActor(BACT *pbact, LegacyLong *pibset)
{
    AssertVarMem(pbact);
    AssertNilOrVarMem(pibset);

    auto services = GeometryServices();
    MMI32 set;
    auto body = static_cast<PBody>(mm_body_from_actor(pbact, pibset ? &set : nullptr, &services));
    if (body && pibset) *pibset = set;
    return body;
}

/***************************************************************************
    Create a duplicate of this Body.  The duplicate will be hidden
    (hidden count 1) regardless of the state of this Body.
***************************************************************************/
PBody EngineBody::Duplicate(PBody body)
{
    BODY_VALIDATE(body, 0);
    const auto &services = EditServices();
    auto copy = static_cast<PBody>(mm_body_duplicate(body, &services));
#ifdef DEBUG
    Validate(copy);
#endif
    return copy;
}

/***************************************************************************
    Replace this Body with pbodyDup.  Preserve this Body's hidden-ness.
***************************************************************************/
void EngineBody::Restore(PBody body, PBody pbodyDup)
{
#ifdef DEBUG
    Validate(pbodyDup);
#endif
    Assert(State(pbodyDup).shape.hidden == 1, "dup hidden count must be 1");
    const auto &services = EditServices();
    mm_body_restore(body, pbodyDup, &services);
    BODY_VALIDATE(body, fobjAssertFull);
}

/***************************************************************************
    Make this Body visible, if it was invisible.  Keeps a refcount.
***************************************************************************/
void EngineBody::Show(PBody body)
{
    BODY_VALIDATE(body, 0);
    Assert(State(body).shape.hidden > 0, "object is already visible!");
    const auto &s = RuntimeServices(); mm_body_show(body, &s);
}

/***************************************************************************
    Make this Body invisible, if it was visible.  Keeps a refcount.
***************************************************************************/
void EngineBody::Hide(PBody body)
{
    BODY_VALIDATE(body, 0);
    const auto &s = RuntimeServices(); mm_body_hide(body, &s);
}

/***************************************************************************
    Sets the hilite color to use.
***************************************************************************/
void EngineBody::SetHiliteColor(LegacyLong iclr)
{
    mm_brender_body_highlight_colour(iclr);
}

/***************************************************************************
    Hilites the Body
***************************************************************************/
void EngineBody::Hilite(PBody body)
{
    BODY_VALIDATE(body, 0);
    const auto &s = RuntimeServices(); mm_body_highlight(body, 1, &s);
}

/***************************************************************************
    Unhilites the Body
***************************************************************************/
void EngineBody::Unhilite(PBody body)
{
    BODY_VALIDATE(body, 0);
    const auto &s = RuntimeServices(); mm_body_highlight(body, 0, &s);
}

/***************************************************************************
    Position the body at (xr, yr, zr) in worldspace	oriented by pbmat34
***************************************************************************/
void EngineBody::LocateOrient(PBody body, BRS xr, BRS yr, BRS zr, BMAT34 *pbmat34)
{
    BODY_VALIDATE(body, 0); AssertVarMem(pbmat34);
    static_assert(sizeof(BRS) == sizeof(MMI32), "body scalar ABI");
    MMActorBodyPose pose;
    pose.translation = {static_cast<MMI32>(xr), static_cast<MMI32>(yr), static_cast<MMI32>(zr)};
    std::memcpy(&pose.orientation, pbmat34, sizeof(pose.orientation));
    ApplyPose(body, pose);
}

void EngineBody::ApplyPose(PBody body, const MMActorBodyPose &pose)
{
    BODY_VALIDATE(body, 0);
    const auto &s = RuntimeServices();
    mm_body_locate(body, &pose, &s);
}

bool EngineBody::PartSetBounds(PBody body, LegacyLong set, MMBodyBounds &out)
{
    return mm_brender_body_set_bounds(body, set, &out) != 0;
}

/***************************************************************************
    Set the ibact'th body part to use model pmodl
***************************************************************************/
void EngineBody::SetPartModel(PBody body, LegacyLong ibact, PModel pmodl)
{
    BODY_VALIDATE(body, 0); AssertIn(ibact, 0, State(body).shape.part_count);
#ifdef DEBUG
    EngineModel::Validate(pmodl);
#endif
    const auto &s = RuntimeServices(); mm_body_set_model(body, ibact, pmodl, &s);
}

/***************************************************************************
    Set the ibact'th body part to use matrix pbmat34
***************************************************************************/
void EngineBody::SetPartMatrix(PBody body, LegacyLong ibact, BMAT34 *pbmat34)
{
    BODY_VALIDATE(body, 0); AssertIn(ibact, 0, State(body).shape.part_count); AssertVarMem(pbmat34);
    MMActorMatrix matrix;
    std::memcpy(&matrix, pbmat34, sizeof(matrix));
    SetPartMatrix(body, ibact, matrix);
}

void EngineBody::SetPartMatrix(PBody body, LegacyLong ibact, const MMActorMatrix &matrix)
{
    BODY_VALIDATE(body, 0); AssertIn(ibact, 0, State(body).shape.part_count);
    const auto &s = RuntimeServices();
    mm_body_set_matrix(body, ibact, &matrix, &s);
}

/***************************************************************************
    Set the ibset'th body part set to use material pmtrl
***************************************************************************/
void EngineBody::SetPartSetMtrl(PBody body, LegacyLong ibset, PMaterial_MTRL pmtrl)
{
    BODY_VALIDATE(body, 0); AssertIn(ibset, 0, State(body).shape.set_count);
#ifdef DEBUG
    EngineMaterial::Validate(pmtrl);
#endif
    const auto &s = RuntimeServices(); mm_body_set_generic(body, ibset, pmtrl, &s);
}

/***************************************************************************
    Apply the given CustomMaterial_CMTL to the appropriate body part set (the CustomMaterial_CMTL knows
    which body part set to apply to).
***************************************************************************/
void EngineBody::SetPartSetCmtl(PBody body, PCustomMaterial_CMTL pcmtl)
{
    BODY_VALIDATE(body, 0);
#ifdef DEBUG
    EngineCostume::Validate(pcmtl);
#endif
    const auto &s = RuntimeServices(); mm_body_set_custom(body, pcmtl, &s);
}

/***************************************************************************
    Determines the current CustomMaterial_CMTL or Material_MTRL applied to the given ibset.
    If a CustomMaterial_CMTL is attached, *pfMtrl is fFalse and *ppcmtl holds the
    PCustomMaterial_CMTL.  If a Material_MTRL is attached, *pfMtrl is fTrue and *ppmtrl holds
    the PMaterial_MTRL.
***************************************************************************/
void EngineBody::GetPartSetMaterial(PBody body, LegacyLong ibset, bool *pfMtrl, PMaterial_MTRL *ppmtrl, PCustomMaterial_CMTL *ppcmtl)
{
    BODY_VALIDATE(body, 0); AssertIn(ibset, 0, State(body).shape.set_count);
    AssertVarMem(pfMtrl); AssertVarMem(ppmtrl); AssertVarMem(ppcmtl);
    void *material, *custom; int generic;
    CopyPb(ppmtrl, &material, sizeof(material)); CopyPb(ppcmtl, &custom, sizeof(custom));
    const auto &s = RuntimeServices(); mm_body_get_material(body, ibset, &generic, &material, &custom, &s);
    *pfMtrl = generic != 0;
    CopyPb(&material, ppmtrl, sizeof(material)); CopyPb(&custom, ppcmtl, sizeof(custom));
}

MMBodyGeometryServices EngineBody::GeometryServices(void)
{
    auto s = mm_kauai_body_renderer()->geometry;
#ifdef DEBUG
    s.owner = [](void *, void *actor) -> void * {
        auto body = static_cast<PBody>(mm_brender_body_owner(actor));
        Validate(body);
        return body;
    };
#endif
    return s;
}

/***************************************************************************
    World is about to render, so clear this Body's current C bounds
    (but save the last good bounds in its last-visible C rectangle).  Also size the
    bounding box correctly.
***************************************************************************/
int EngineBody::ReadRenderBody(PBACT root, MMRenderBody *entry)
{
    const auto services = GeometryServices();
    const auto body = static_cast<PBody>(mm_body_read_render(root, &entry->draw, &services));
    if (!body)
        return 0;
    entry->target = root;
    entry->resource = body;
    return 1;
}

void EngineBody::PrepareRenderBody(const MMRenderBody *entry)
{
    mm_brender_body_submit(&entry->draw, entry->target);
    const auto services = GeometryServices();
    mm_body_prepare_render(entry->target, &services);
}

/***************************************************************************
    Return the bounds of the Body contaning PBACT
***************************************************************************/
void EngineBody::RenderRect(PBACT pbact, RC *prc)
{
    AssertVarMem(pbact);
    AssertVarMem(prc);

    auto services = GeometryServices();
    MMBodyRect rect;
    if (mm_body_render_rect(pbact, &rect, &services))
        prc->Set(rect.left, rect.top, rect.right, rect.bottom);
}

/***************************************************************************
    Compute the world-space bounding box of the Body.  The code temporarily
    changes the hilite BACT's type to BR_ACTOR_NONE so that BrActorToBounds
    doesn't include the size of the bounding box when computing the size of
    the actor.
***************************************************************************/
void EngineBody::GetBcbBounds(PBody body, BCB *pbcb, bool fWorld)
{
    BODY_VALIDATE(body, 0);
    AssertVarMem(pbcb);

    MMBodyBounds bounds;
    mm_brender_body_bounds(mm_kauai_body_renderer(), body, fWorld, &bounds);
    pbcb->xrMin = bounds.min[0]; pbcb->yrMin = bounds.min[1]; pbcb->zrMin = bounds.min[2];
    pbcb->xrMax = bounds.max[0]; pbcb->yrMax = bounds.max[1]; pbcb->zrMax = bounds.max[2];
}

/***************************************************************************
    World calls this function when each BACT is rendered.  It unions the
    BACT bounds with the Body's current C rectangle
***************************************************************************/
void EngineBody::ActorRendered(PBACT pbact, RC *prc)
{
    AssertVarMem(pbact);
    AssertVarMem(prc);
    auto services = GeometryServices();
    const MMBodyRect rect = {prc->xpLeft, prc->ypTop, prc->xpRight, prc->ypBottom};
    mm_body_rendered(pbact, &rect, &services);
}

/***************************************************************************
    Returns whether the Body was in view the last time it was rendered.
***************************************************************************/
bool EngineBody::IsInView(PBody body)
{
    BODY_VALIDATE(body, 0);
    return mm_body_in_view(&State(body).geometry) != 0;
}

/***************************************************************************
    Fills in the 2D bounds of the Body, the last time it was rendered.  If
    the Body was not in view at the last render, this function fills in the
    bounds of the Body the last time it was onstage.
***************************************************************************/
void EngineBody::GetRcBounds(PBody body, RC *prc)
{
    BODY_VALIDATE(body, 0);
    AssertVarMem(prc);

    MMBodyRect rect;
    mm_body_visible_rect(&State(body).geometry, &rect);
    prc->Set(rect.left, rect.top, rect.right, rect.bottom);
}

/***************************************************************************
    Fills in the 2D coordinates of the center of the Body, the last time
    it was rendered.  If the Body was not in view at the last render, this
    function fills in the center of the Body the last time it was onstage.
***************************************************************************/
void EngineBody::GetCenter(PBody body, LegacyLong *pxp, LegacyLong *pyp)
{
    BODY_VALIDATE(body, 0);
    AssertVarMem(pxp);
    AssertVarMem(pyp);

    MMI32 x, y;
    mm_body_center(&State(body).geometry, &x, &y);
    *pxp = x; *pyp = y;
}

/***************************************************************************
    Fills in the current position of the origin of this Body.
***************************************************************************/
void EngineBody::GetPosition(PBody body, BRS *pxr, BRS *pyr, BRS *pzr)
{
    BODY_VALIDATE(body, 0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);

    MMI32 position[3];
    mm_body_position(&State(body).shape, position);
    *pxr = position[0]; *pyr = position[1]; *pzr = position[2];
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the Body.
***************************************************************************/
void EngineBody::Validate(PBody body, LegacyUlong grf)
{
    LegacyLong ibact;
    LegacyLong ibset;
    BACT *pbact;

    if (!body)
        return;
    AssertIn(State(body).shape.hidden, 0, 100); // 100 is sanity check
    AssertIn(State(body).shape.set_count, 0, State(body).shape.part_count + 1);
    size_t bytes = 0, offset = 0;
    AssertDo(mm_brender_body_layout(ActorCount(body), kcbMax, &bytes, &offset), "bad body actor storage size");
    AssertPvCb(Root(body), static_cast<LegacyLong>(bytes));
    const auto storage = static_cast<unsigned char *>(State(body).shape.actors);
    Assert(State(body).shape.bindings == reinterpret_cast<MMBodyPartBinding *>(storage + offset),
           "bad body binding storage");
    AssertPvCb(State(body).shape.custom_materials, LwMul(State(body).shape.set_count, size(void *)));
    Assert(pvNil == Root(body)->model, "Body root shouldn't have a model!!");
    Assert(pvNil == Root(body)->material, "Body root shouldn't have a material!!");

    if (grf & fobjAssertFull)
    {
        for (ibact = 0; ibact < State(body).shape.part_count; ibact++)
        {
            pbact = Part(body, ibact);
            const auto &binding = State(body).shape.bindings[ibact+2];
            AssertIn(binding.set, 0, State(body).shape.set_count);
            if (pvNil != pbact->model)
                EngineModel::Validate(EngineModel::FromHandle(pbact->model));
            Assert(binding.material == pbact->material, "body part material binding mismatch");
            if (pvNil != pbact->material)
            {
                auto material = EngineMaterial::FromHandle(pbact->material);
                EngineMaterial::Validate(material);
                Assert(!binding.material_owner || binding.material_owner == material, "body part material owner mismatch");
            }
        }
        for (ibset = 0; ibset < State(body).shape.set_count; ibset++)
        {
            if (auto costume = static_cast<PCustomMaterial_CMTL>(State(body).shape.custom_materials[ibset]))
                EngineCostume::Validate(costume);
        }
    }
}

/***************************************************************************
    Mark memory used by the Body
***************************************************************************/
void EngineBody::Mark(PBody body)
{
    if (!body)
        return;
    Validate(body);

    MarkPv(body);
    MarkPv(Root(body));
    MarkPv(State(body).shape.custom_materials);
    for (LegacyLong part = 0; part < State(body).shape.part_count; ++part)
    {
        const auto &binding = State(body).shape.bindings[part+2];
        if (binding.model_owner)
            EngineModel::Mark(static_cast<PModel>(binding.model_owner));
        if (binding.material_owner)
            EngineMaterial::Mark(static_cast<PMaterial_MTRL>(binding.material_owner));
    }
    for (LegacyLong set = 0; set < State(body).shape.set_count; ++set)
        if (State(body).shape.custom_materials[set])
            EngineMaterial::Mark(static_cast<PCustomMaterial_CMTL>(State(body).shape.custom_materials[set]));
}
#endif // DEBUG
