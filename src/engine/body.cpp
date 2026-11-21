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
    root is _Actors()[0].  The hilite BACT is _Actors()[1].  The real
    body parts start at _Actors()[2], with _shape.part_count entries.
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

    The way that MODLs, MTRLs, and CMTLs attach to the Body is a little
    strange.  In the case of models, the br_actor's model needs to be set
    to the Model's BMDL (which is a BRender data structure), not the
    Model itself (which is a Socrates data structure).  When it is time
    to remove the model, the Body has a pointer to the BMDL, but
    not the Model!  Fortunately, Model sets the BMDL's identifier field
    to point to its owning Model.  So ReleasePpo can then be called on the
    Model.  But I don't want to set the BMDL's identifier to pvNil in
    the process, because other Bodys might be counting on the BMDL's
    identifier to point to the Model.  So this code has to be careful using
    ReleasePpo, since that sets the given pointer to pvNil.  Same
    problem for MTRLs:

    Body
     |					 Model<--+
     v					   |	|
     BACT   			   |	|
      | |				   |	|
      |	v				   v	|
      |model------------>BMDL	|
      |					   |	|
      |					   v	|
      |					  identifier
      |
      +--+
         |			     Material_MTRL<--+
         |                 |	|
         v				   v	|
       material--------->BMTL	|
                           |	|
                           v	|
                          identifier

    CMTLs are potentially even messier, since they're an abstraction on
    top of MTRLs.  Here, an array of PCMTLs on the Body is explicitly kept
    so it is easy to find out what CustomMaterial_CMTL is attached to what body part set
    instead of working backwards from the BACT's material field.

***************************************************************************/
#include "soc_core.h"
#include "body_renderer.h"
#include "engine_part_indices.h"
#include <cstring>
ASSERTNAME

RTCLASS(Body)
RTCLASS(BodyCostume)

static constexpr size_t kcbBodyActor = sizeof(BACT) + sizeof(MMBodyPartBinding);
static constexpr size_t kBodyBindingAlign = alignof(MMBodyPartBinding);

static size_t BodyBindingOffset(size_t count)
{
    const size_t bytes = count * sizeof(BACT);
    return (bytes + kBodyBindingAlign - 1) & ~(kBodyBindingAlign - 1);
}

static size_t BodyStorageSize(size_t count)
{
    return BodyBindingOffset(count) + count * sizeof(MMBodyPartBinding);
}

// Specification of hilite color.  REVIEW *****: should these
// values (or the PBMTL itself?) be passed in by the client?
const br_colour kbrcHilite = BR_COLOUR_RGB(255, 255, 255);
const byte kbOpaque = 0xff;
const br_ufraction kbrufKaHilite = BR_UFRACTION(0.10);
const br_ufraction kbrufKdHilite = BR_UFRACTION(0.60);
const br_ufraction kbrufKsHilite = BR_UFRACTION(0.60);
const BRS krPowerHilite = BR_SCALAR(50);
const LegacyLong kiclrHilite = 0; // default to no highlighting

// Hilighting material
PBMTL Body::_pbmtlHilite = pvNil;

void Body::ReleaseSharedMaterial(void)
{
    if (_pbmtlHilite != pvNil)
    {
        BrMaterialRemove(_pbmtlHilite);
        BrMaterialFree(_pbmtlHilite);
        _pbmtlHilite = pvNil;
    }
}

/***************************************************************************
    Set the optional renderer binding while this body is hidden.
***************************************************************************/
void Body::SetRenderCallbacks(void *context, void (*attach)(void *, Body *), void (*changed)(void *))
{
    Assert(_shape.hidden > 0, "Cannot rebind a visible body");
    _pvRenderContext = context;
    _pfnAttach = attach;
    _pfnChanged = changed;
}

/***************************************************************************
    Builds a tree of BACTs for the Body.
***************************************************************************/
MMBodyCreateServices Body::_CreateServices(void)
{
    MMBodyCreateServices s = {};
    s.state = [](void *, void *object) -> MMBodyShape * { return &static_cast<PBody>(object)->_shape; };
    s.highlight = [](void *) -> void * { return _pbmtlHilite; };
    s.allocate_highlight = [](void *) -> void * { return BrMaterialAllocate("Hilite"); };
    s.install_highlight = [](void *, void *material) {
        _pbmtlHilite = static_cast<PBMTL>(material);
        _pbmtlHilite->colour = kbrcHilite;
        _pbmtlHilite->ka = kbrufKaHilite;
        _pbmtlHilite->kd = kbrufKdHilite; _pbmtlHilite->ks = kbrufKsHilite;
        _pbmtlHilite->power = krPowerHilite;
        _pbmtlHilite->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD | BR_MATF_FORCE_Z_0;
        _pbmtlHilite->index_base = kiclrHilite; _pbmtlHilite->index_range = 1;
        BrMaterialAdd(_pbmtlHilite);
    };
    s.create = [](void *) -> void * { return NewObj Body; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.validate_lists = [](void *, void *parents, void *sets) {
        const auto par = static_cast<const MMPartIndexView *>(parents);
        const auto list = static_cast<const MMPartIndexView *>(sets);
        Assert(par->count == 0 || ivNil == mm_part_index_get(par, 0), "bad first parent index");
        Assert(par->count == list->count, "body parent and set lists must have the same size");
        (void)par; (void)list;
    };
    s.duplicate_sets = [](void *, void *sets) -> void * {
        auto list = EnginePartIndices::Create();
        if (list && !mm_part_indices_copy(list, static_cast<const MMPartIndexView *>(sets)))
        {
            mm_part_indices_dispose(list);
            return nullptr;
        }
        return list;
    };
    s.release_sets = [](void *, void *sets) { mm_part_indices_dispose(static_cast<MMPartIndexList *>(sets)); };
    // An owned list begins with the same view passed by borrowed callers.
    s.count = [](void *, void *list) -> MMI32 { return static_cast<const MMPartIndexView *>(list)->count; };
    s.item = [](void *, void *list, MMI32 i) -> MMI32 {
        return mm_part_index_get(static_cast<const MMPartIndexView *>(list), i);
    };
    s.allocate_materials = [](void *, MMI32 count, void **out) -> int {
        return FAllocPv(out, LwMul(count, size(PCustomMaterial_CMTL)), fmemClear, mprNormal);
    };
    s.allocate_actors = [](void *, MMI32 count, void **actors, MMBodyPartBinding **bindings) -> int {
        if (count < 0 || static_cast<size_t>(count) > (kcbMax - kBodyBindingAlign) / kcbBodyActor ||
            !FAllocPv(actors, static_cast<LegacyLong>(BodyStorageSize(count)), fmemClear, mprNormal))
            return 0;
        auto storage = static_cast<unsigned char *>(*actors);
        *bindings = reinterpret_cast<MMBodyPartBinding *>(storage + BodyBindingOffset(count));
        return 1;
    };
    s.initialize_actor = [](void *, void *body, void *actors, MMI32 index, int role) {
        auto actor = static_cast<PBACT>(actors) + index;
        actor->type = role == MM_BODY_PART ? BR_ACTOR_MODEL : BR_ACTOR_NONE;
        actor->t.type = BR_TRANSFORM_MATRIX34;
        actor->identifier = static_cast<schar *>(body);
        if (role == MM_BODY_HIGHLIGHT) {
            BrMatrix34Identity(&actor->t.t.mat);
            actor->material = _pbmtlHilite; actor->render_style = BR_RSTYLE_BOUNDING_EDGES;
        } else if (role == MM_BODY_PART) actor->render_style = BR_RSTYLE_FACES;
    };
    s.assign_matrix = mm_brender_body_matrix;
    s.link = [](void *, void *actors, MMI32 parent, MMI32 child) {
        AssertIn(parent, 0, child);
        Assert(parent != 1, "body part parent cannot be highlight actor");
        auto array = static_cast<PBACT>(actors); BrActorAdd(array + parent, array + child);
    };
    s.hide = [](void *, void *body) { static_cast<PBody>(body)->Hide(); };
    s.remove_material = [](void *, void *body, MMI32 set) { static_cast<PBody>(body)->_RemoveMaterial(set); };
    s.free_memory = [](void *, void **memory) { FreePpv(memory); };
    return s;
}

Body *Body::PbodyNew(PDynamicArray pglibactPar, PDynamicArray pglibset)
{
    return PbodyNew(EnginePartIndices::Borrow(pglibactPar), EnginePartIndices::Borrow(pglibset));
}

Body *Body::PbodyNew(const MMPartIndexView &parents, const MMPartIndexView &sets)
{
    auto services = _CreateServices();
    auto body = static_cast<PBody>(mm_body_create(const_cast<MMPartIndexView *>(&parents),
                                                 const_cast<MMPartIndexView *>(&sets), &services));
    AssertNilOrPo(body, fobjAssertFull);
    return body;
}

/***************************************************************************
    Change the body part hierarchy and/or the body part sets of this Body.
    Models, materials, and matrices are not changed for body parts that
    exist in both the old and reshaped Bodys.
***************************************************************************/
bool Body::FChangeShape(PDynamicArray pglibactPar, PDynamicArray pglibset)
{
    return FChangeShape(EnginePartIndices::Borrow(pglibactPar), EnginePartIndices::Borrow(pglibset));
}

bool Body::FChangeShape(const MMPartIndexView &parents, const MMPartIndexView &sets)
{
    AssertThis(fobjAssertFull);
    auto services = _EditServices();
    const bool ok = mm_body_change_shape(this, const_cast<MMPartIndexView *>(&parents),
                                         const_cast<MMPartIndexView *>(&sets), &services) != 0;
    AssertThis(fobjAssertFull);
    return ok;
}

/***************************************************************************
    Returns the Body that owns the given BACT.  Also, if pibset is not
    nil, returns what body part set this BACT is in.  If the pbact is
    the hilite BACT, pibset is set to ivNil.
***************************************************************************/
Body *Body::PbodyFromBact(BACT *pbact, LegacyLong *pibset)
{
    AssertVarMem(pbact);
    AssertNilOrVarMem(pibset);

    auto services = _GeometryServices();
    MMI32 set;
    auto body = static_cast<PBody>(mm_body_from_actor(pbact, pibset ? &set : nullptr, &services));
    if (body && pibset) *pibset = set;
    return body;
}

/***************************************************************************
    Destroy the Body's body parts, including their attached materials and
    models.
***************************************************************************/
void Body::_DestroyShape(void)
{
    AssertBaseThis(0);
    auto services = _CreateServices();
    mm_body_shape_dispose(this, &services);
}

/***************************************************************************
    Free the Body and all attached MODLs, MTRLs, and CMTLs.
***************************************************************************/
Body::~Body(void)
{
    AssertBaseThis(0);
    _DestroyShape();
}

/***************************************************************************
    Create a duplicate of this Body.  The duplicate will be hidden
    (_shape.hidden == 1) regardless of the state of this Body.
***************************************************************************/
MMBodyEditServices Body::_EditServices(void)
{
    MMBodyEditServices s = {};
    s.shape = _CreateServices();
    s.copy_bindings = [](void *, void *to, void *from) {
        auto dst = static_cast<PBody>(to); auto src = static_cast<PBody>(from);
        dst->_pvRenderContext = src->_pvRenderContext; dst->_pfnAttach = src->_pfnAttach;
        dst->_pfnChanged = src->_pfnChanged; dst->_geometry.found = src->_geometry.found; dst->_geometry.set = src->_geometry.set;
    };
    s.copy_actors = [](void *, void *to, void *from, MMI32 count) { CopyPb(from, to, LwMul(count, size(BACT))); };
    s.rebase_actor = [](void *, void *body, void *to, void *from, MMI32 index, int root) {
        auto actor = static_cast<PBACT>(to) + index;
        const LegacyLong delta = BvSubPvs(to, from);
        actor->identifier = static_cast<schar *>(body);
        if (root) {
            actor->parent = pvNil; actor->next = pvNil; actor->prev = pvNil;
        } else {
            if (actor->parent) actor->parent = static_cast<PBACT>(PvAddBv(actor->parent, delta));
            if (actor->next) actor->next = static_cast<PBACT>(PvAddBv(actor->next, delta));
            if (actor->prev) actor->prev = static_cast<PBACT *>(PvAddBv(actor->prev, delta));
        }
        if (actor->children) actor->children = static_cast<PBACT>(PvAddBv(actor->children, delta));
    };
    s.copy_materials = [](void *, void *to, void *from, MMI32 count) {
        CopyPb(from, to, LwMul(count, size(PCustomMaterial_CMTL)));
    };
    s.copy_bounds = [](void *, void *to, void *from) {
        auto dst = static_cast<PBody>(to); auto src = static_cast<PBody>(from);
        dst->_geometry.bounds = src->_geometry.bounds; dst->_geometry.last_visible = src->_geometry.last_visible;
    };
    s.material = [](void *, void *body, MMI32 set, int *generic) -> void * {
        bool isGeneric; PMaterial_MTRL material; PCustomMaterial_CMTL custom;
        static_cast<PBody>(body)->GetPartSetMaterial(set, &isGeneric, &material, &custom);
        *generic = isGeneric;
        if (isGeneric) return material;
        return custom;
    };
    s.retain = [](void *, void *object) { static_cast<BASE *>(object)->AddRef(); };
    s.show = [](void *, void *body) { static_cast<PBody>(body)->Show(); };
    s.swap_bodies = [](void *, void *a, void *b) {
        auto body = static_cast<PBody>(a); auto replacement = static_cast<PBody>(b);
        AssertPo(replacement, 0); Assert(replacement->_shape.hidden == 1, "dup hidden count must be 1");
        SwapVars(body, replacement);
    };
    s.identify = [](void *, void *body, void *actors, MMI32 index) {
        (static_cast<PBACT>(actors) + index)->identifier = static_cast<schar *>(body);
    };
    s.set_generic = [](void *, void *body, MMI32 set, void *material) {
        static_cast<PBody>(body)->SetPartSetMtrl(set, static_cast<PMaterial_MTRL>(material));
    };
    s.set_custom = [](void *, void *body, void *material) {
        static_cast<PBody>(body)->SetPartSetCmtl(static_cast<PCustomMaterial_CMTL>(material));
    };
    s.set_model = [](void *, void *body, MMI32 part, void *model) {
        static_cast<PBody>(body)->SetPartModel(part, static_cast<PModel>(model));
    };
    s.highlighted = [](void *, void *actors) -> int { return static_cast<PBACT>(actors)[1].type == BR_ACTOR_MODEL; };
    s.highlight = [](void *, void *body) { static_cast<PBody>(body)->Hilite(); };
    return s;
}

PBody Body::PbodyDup(void)
{
    AssertThis(0);
    auto services = _EditServices();
    auto body = static_cast<PBody>(mm_body_duplicate(this, &services));
    AssertNilOrPo(body, 0);
    return body;
}

/***************************************************************************
    Replace this Body with pbodyDup.  Preserve this Body's hidden-ness.
***************************************************************************/
void Body::Restore(PBody pbodyDup)
{
    AssertBaseThis(0);
    AssertPo(pbodyDup, 0);
    Assert(pbodyDup->_shape.hidden == 1, "dup hidden count must be 1");
    auto services = _EditServices();
    mm_body_restore(this, pbodyDup, &services);
    AssertThis(fobjAssertFull);
}

/***************************************************************************
    Make this Body visible, if it was invisible.  Keeps a refcount.
***************************************************************************/
MMBodyRuntimeServices Body::_RuntimeServices(void)
{
    MMBodyRuntimeServices s = {};
    s.shape = _CreateServices();
    s.retain = [](void *, void *object) { static_cast<BASE *>(object)->AddRef(); };
    s.model_handle = [](void *, void *object) -> void * {
        auto model = static_cast<PModel>(object);
        auto handle = model->Pbmdl();
        Assert(Model::PmodlFromBmdl(handle) == model, "Model problem");
        return handle;
    };
    s.assign_model = mm_brender_body_model;
    s.assign_material = mm_brender_body_material;
    s.material_object = [](void *, void *raw) -> void * { return Material_MTRL::PmtrlFromBmtl(static_cast<PBMTL>(raw)); };
    s.material_handle = [](void *, void *object) -> void * { return static_cast<PMaterial_MTRL>(object)->Pbmtl(); };
    s.custom = [](void *, void *body, MMI32 set) -> void * { return static_cast<PBody>(body)->_Materials()[set]; };
    s.assign_custom = [](void *, void *body, MMI32 set, void *object) { static_cast<PBody>(body)->_Materials()[set] = static_cast<PCustomMaterial_CMTL>(object); };
    s.custom_set = [](void *, void *object) -> MMI32 { return static_cast<PCustomMaterial_CMTL>(object)->Ibset(); };
    s.custom_count = [](void *, void *object) -> MMI32 { return static_cast<PCustomMaterial_CMTL>(object)->Cbprt(); };
    s.custom_model = [](void *, void *object, MMI32 i) -> void * { return static_cast<PCustomMaterial_CMTL>(object)->Pmodl(i); };
    s.custom_material = [](void *, void *object, MMI32 i) -> void * { return static_cast<PCustomMaterial_CMTL>(object)->Pbmtl(i); };
    s.invalidate_output = [](void *, void **out) { TrashVar(out); };
    s.highlight = [](void *, void *body, int enabled) { static_cast<PBody>(body)->_PbactHilite()->type = enabled ? BR_ACTOR_MODEL : BR_ACTOR_NONE; };
    s.attach = [](void *, void *object) { auto body = static_cast<PBody>(object); if (body->_pfnAttach) body->_pfnAttach(body->_pvRenderContext, body); };
    s.attached = [](void *, void *body) -> int { return static_cast<PBody>(body)->_PbactRoot()->parent != pvNil; };
    s.detach = [](void *, void *body) { BrActorRemove(static_cast<PBody>(body)->_PbactRoot()); };
    s.clear_bounds = [](void *, void *body) { mm_body_clear_bounds(&static_cast<PBody>(body)->_geometry); };
    s.changed = [](void *, void *body) { static_cast<PBody>(body)->_RenderChanged(); };
    s.is_generic = [](void *, void *object) -> int { return static_cast<BASE *>(object)->FIs(kclsMaterial_MTRL); };
    s.diagnostic = [](void *, int code) {
        switch (code) {
        case MM_BODY_MISSING_MATERIAL: Assert(false, "body set has no material"); break;
        case MM_BODY_EMPTY_SET: Assert(false, "empty body set"); break;
        case MM_BODY_SHORT_COSTUME: Assert(false, "not enough custom materials!"); break;
        case MM_BODY_COSTUME_SHAPE: Assert(false, "different Body shapes!"); break;
        }
    };
    return s;
}

void Body::Show(void)
{
    AssertThis(0);
    Assert(_shape.hidden > 0, "object is already visible!");
    auto s = _RuntimeServices(); mm_body_show(this, &s);
}

/***************************************************************************
    Make this Body invisible, if it was visible.  Keeps a refcount.
***************************************************************************/
void Body::Hide(void)
{
    AssertThis(0);
    auto s = _RuntimeServices(); mm_body_hide(this, &s);
}

/***************************************************************************
    Sets the hilite color to use.
***************************************************************************/
void Body::SetHiliteColor(LegacyLong iclr)
{
    if (_pbmtlHilite != pvNil)
    {
        _pbmtlHilite->index_base = (uint8_t)iclr;
    }
}

/***************************************************************************
    Hilites the Body
***************************************************************************/
void Body::Hilite(void)
{
    AssertThis(0);
    auto s = _RuntimeServices(); mm_body_highlight(this, 1, &s);
}

/***************************************************************************
    Unhilites the Body
***************************************************************************/
void Body::Unhilite(void)
{
    AssertThis(0);
    auto s = _RuntimeServices(); mm_body_highlight(this, 0, &s);
}

/***************************************************************************
    Position the body at (xr, yr, zr) in worldspace	oriented by pbmat34
***************************************************************************/
void Body::LocateOrient(BRS xr, BRS yr, BRS zr, BMAT34 *pbmat34)
{
    AssertThis(0); AssertVarMem(pbmat34);
    static_assert(sizeof(BRS) == sizeof(MMI32), "body scalar ABI");
    MMActorBodyPose pose;
    pose.translation = {static_cast<MMI32>(xr), static_cast<MMI32>(yr), static_cast<MMI32>(zr)};
    std::memcpy(&pose.orientation, pbmat34, sizeof(pose.orientation));
    ApplyPose(pose);
}

void Body::ApplyPose(const MMActorBodyPose &pose)
{
    AssertThis(0);
    auto s = _RuntimeServices();
    mm_body_locate(this, &pose, &s);
}

void Body::PartWorldMatrix(LegacyLong part, MMActorMatrix &out) const
{
    AssertIn(part, 0, _shape.part_count);
    mm_body_shape_part_world(&_shape, part, &out);
}

/***************************************************************************
    Set the ibact'th body part to use model pmodl
***************************************************************************/
void Body::SetPartModel(LegacyLong ibact, Model *pmodl)
{
    AssertThis(0); AssertIn(ibact, 0, _shape.part_count); AssertPo(pmodl, 0);
    auto s = _RuntimeServices(); mm_body_set_model(this, ibact, pmodl, &s);
}

/***************************************************************************
    Set the ibact'th body part to use matrix pbmat34
***************************************************************************/
void Body::SetPartMatrix(LegacyLong ibact, BMAT34 *pbmat34)
{
    AssertThis(0); AssertIn(ibact, 0, _shape.part_count); AssertVarMem(pbmat34);
    MMActorMatrix matrix;
    std::memcpy(&matrix, pbmat34, sizeof(matrix));
    SetPartMatrix(ibact, matrix);
}

void Body::SetPartMatrix(LegacyLong ibact, const MMActorMatrix &matrix)
{
    AssertThis(0); AssertIn(ibact, 0, _shape.part_count);
    auto s = _RuntimeServices();
    mm_body_set_matrix(this, ibact, &matrix, &s);
}

/***************************************************************************
    Remove old Material_MTRL or CustomMaterial_CMTL from ibset.  This is nontrivial because there
    could either be a CustomMaterial_CMTL attached to the bset (in which case we just free
    the CustomMaterial_CMTL) or a bunch of MTRLs (in which case we free each Material_MTRL).
    Actually, in the latter case, it would be a bunch of copies of the same
    Material_MTRL, but we keep one reference per body part in the set.
***************************************************************************/
void Body::_RemoveMaterial(LegacyLong ibset)
{
    AssertThis(0); AssertIn(ibset, 0, _shape.set_count);
    auto s = _RuntimeServices(); mm_body_remove_material(this, ibset, &s);
}

/***************************************************************************
    Set the ibset'th body part set to use material pmtrl
***************************************************************************/
void Body::SetPartSetMtrl(LegacyLong ibset, Material_MTRL *pmtrl)
{
    AssertThis(0); AssertIn(ibset, 0, _shape.set_count); AssertPo(pmtrl, 0);
    auto s = _RuntimeServices(); mm_body_set_generic(this, ibset, pmtrl, &s);
}

/***************************************************************************
    Apply the given CustomMaterial_CMTL to the appropriate body part set (the CustomMaterial_CMTL knows
    which body part set to apply to).
***************************************************************************/
void Body::SetPartSetCmtl(CustomMaterial_CMTL *pcmtl)
{
    AssertThis(0); AssertPo(pcmtl, 0);
    auto s = _RuntimeServices(); mm_body_set_custom(this, pcmtl, &s);
}

/***************************************************************************
    Determines the current CustomMaterial_CMTL or Material_MTRL applied to the given ibset.
    If a CustomMaterial_CMTL is attached, *pfMtrl is fFalse and *ppcmtl holds the
    PCustomMaterial_CMTL.  If a Material_MTRL is attached, *pfMtrl is fTrue and *ppmtrl holds
    the PMaterial_MTRL.
***************************************************************************/
void Body::GetPartSetMaterial(LegacyLong ibset, bool *pfMtrl, Material_MTRL **ppmtrl, CustomMaterial_CMTL **ppcmtl)
{
    AssertThis(0); AssertIn(ibset, 0, _shape.set_count);
    AssertVarMem(pfMtrl); AssertVarMem(ppmtrl); AssertVarMem(ppcmtl);
    void *material, *custom; int generic;
    CopyPb(ppmtrl, &material, sizeof(material)); CopyPb(ppcmtl, &custom, sizeof(custom));
    auto s = _RuntimeServices(); mm_body_get_material(this, ibset, &generic, &material, &custom, &s);
    *pfMtrl = generic != 0;
    CopyPb(&material, ppmtrl, sizeof(material)); CopyPb(&custom, ppcmtl, sizeof(custom));
}

MMBodyGeometryServices Body::_GeometryServices(void)
{
    MMBodyGeometryServices s = {};
    s.owner = [](void *, void *actor) -> void * {
        auto body = reinterpret_cast<PBody>(static_cast<PBACT>(actor)->identifier);
        AssertNilOrPo(body, 0); return body;
    };
    s.state = [](void *, void *body) -> MMBodyGeometry * { return &static_cast<PBody>(body)->_geometry; };
    s.part_count = [](void *, void *body) -> MMI32 { return static_cast<PBody>(body)->_shape.part_count; };
    s.part = [](void *, void *body, MMI32 part) -> void * { return static_cast<PBody>(body)->_PbactPart(part); };
    s.part_set = [](void *, void *body, MMI32 part) -> MMI32 { return static_cast<PBody>(body)->_Ibset(part); };
    s.highlight = [](void *, void *body) -> void * { return static_cast<PBody>(body)->_PbactHilite(); };
    s.actor_type = [](void *, void *actor) -> int { return static_cast<PBACT>(actor)->type; };
    s.set_actor_type = [](void *, void *actor, int type) { static_cast<PBACT>(actor)->type = static_cast<byte>(type); };
    s.local_bounds = [](void *, void *body, MMBodyBounds *out) {
        BRB bounds; BrActorToBounds(&bounds, static_cast<PBody>(body)->_PbactRoot());
        for (int i = 0; i < 3; ++i) { out->min[i] = bounds.min.v[i]; out->max[i] = bounds.max.v[i]; }
    };
    s.root_matrix = [](void *, void *body, MMActorMatrix *out) {
        *out = static_cast<PBody>(body)->RootMatrix();
    };
    s.highlight_matrix = [](void *, void *body, const MMBodyBounds *in) {
        BRB bounds;
        for (int i = 0; i < 3; ++i) { bounds.min.v[i] = in->min[i]; bounds.max.v[i] = in->max[i]; }
        BrBoundsToMatrix34(&static_cast<PBody>(body)->_PbactHilite()->t.t.mat, &bounds);
    };
    s.missing_owner = [](void *) { Bug("What actor is this?  It has no Body"); };
    s.none_type = BR_ACTOR_NONE; s.model_type = BR_ACTOR_MODEL;
    return s;
}

/***************************************************************************
    World is about to render, so clear this Body's current C bounds
    (but save the last good bounds in its last-visible C rectangle).  Also size the
    bounding box correctly.
***************************************************************************/
int Body::_ReadRenderBody(PBACT root, MMRenderBody *entry)
{
    const auto services = _GeometryServices();
    const auto body = static_cast<PBody>(services.owner(services.context, root));
    if (!body || root != body->_PbactRoot())
        return 0;
    entry->draw = body->DrawView();
    entry->target = root;
    entry->resource = static_cast<BASE *>(body);
    return 1;
}

void Body::_PrepareRenderBody(const MMRenderBody *entry)
{
    mm_brender_body_submit(&entry->draw, entry->target);
    const auto services = _GeometryServices();
    mm_body_prepare_render(entry->target, &services);
}

/***************************************************************************
    Return the bounds of the Body contaning PBACT
***************************************************************************/
void Body::_GetRc(PBACT pbact, RC *prc)
{
    AssertVarMem(pbact);
    AssertVarMem(prc);

    auto services = _GeometryServices();
    MMBodyRect rect;
    static_assert(sizeof(rect) == sizeof(*prc), "body rectangle bridge");
    CopyPb(prc, &rect, sizeof(rect));
    mm_body_render_rect(pbact, &rect, &services);
    CopyPb(&rect, prc, sizeof(rect));
}

/***************************************************************************
    Compute the world-space bounding box of the Body.  The code temporarily
    changes the hilite BACT's type to BR_ACTOR_NONE so that BrActorToBounds
    doesn't include the size of the bounding box when computing the size of
    the actor.
***************************************************************************/
void Body::GetBcbBounds(BCB *pbcb, bool fWorld)
{
    AssertThis(0);
    AssertVarMem(pbcb);

    auto services = _GeometryServices();
    MMBodyBounds bounds;
    mm_body_bounds(this, fWorld, &bounds, &services);
    pbcb->xrMin = bounds.min[0]; pbcb->yrMin = bounds.min[1]; pbcb->zrMin = bounds.min[2];
    pbcb->xrMax = bounds.max[0]; pbcb->yrMax = bounds.max[1]; pbcb->zrMax = bounds.max[2];
}

/***************************************************************************
    World calls this function when each BACT is rendered.  It unions the
    BACT bounds with the Body's current C rectangle
***************************************************************************/
void Body::_BactRendered(PBACT pbact, RC *prc)
{
    AssertVarMem(pbact);
    AssertVarMem(prc);
    auto services = _GeometryServices();
    const MMBodyRect rect = {prc->xpLeft, prc->ypTop, prc->xpRight, prc->ypBottom};
    mm_body_rendered(pbact, &rect, &services);
}

/***************************************************************************
    Returns whether the Body was in view the last time it was rendered.
***************************************************************************/
bool Body::FIsInView(void)
{
    AssertThis(0);
    return mm_body_in_view(&_geometry) != 0;
}

/***************************************************************************
    Fills in the 2D bounds of the Body, the last time it was rendered.  If
    the Body was not in view at the last render, this function fills in the
    bounds of the Body the last time it was onstage.
***************************************************************************/
void Body::GetRcBounds(RC *prc)
{
    AssertThis(0);
    AssertVarMem(prc);

    MMBodyRect rect;
    mm_body_visible_rect(&_geometry, &rect);
    prc->Set(rect.left, rect.top, rect.right, rect.bottom);
}

/***************************************************************************
    Fills in the 2D coordinates of the center of the Body, the last time
    it was rendered.  If the Body was not in view at the last render, this
    function fills in the center of the Body the last time it was onstage.
***************************************************************************/
void Body::GetCenter(LegacyLong *pxp, LegacyLong *pyp)
{
    AssertThis(0);
    AssertVarMem(pxp);
    AssertVarMem(pyp);

    MMI32 x, y;
    mm_body_center(&_geometry, &x, &y);
    *pxp = x; *pyp = y;
}

/***************************************************************************
    Fills in the current position of the origin of this Body.
***************************************************************************/
void Body::GetPosition(BRS *pxr, BRS *pyr, BRS *pzr)
{
    AssertThis(0);
    AssertVarMem(pxr);
    AssertVarMem(pyr);
    AssertVarMem(pzr);

    auto services = _GeometryServices();
    MMI32 position[3];
    mm_body_position(this, position, &services);
    *pxr = position[0]; *pyr = position[1]; *pzr = position[2];
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the Body.
***************************************************************************/
void Body::AssertValid(LegacyUlong grf)
{
    LegacyLong ibact;
    LegacyLong ibset;
    BACT *pbact;

    Body_PAR::AssertValid(fobjAllocated);
    AssertIn(_shape.hidden, 0, 100); // 100 is sanity check
    AssertIn(_shape.set_count, 0, _shape.part_count + 1);
    AssertPvCb(_Actors(), static_cast<LegacyLong>(BodyStorageSize(_Cbact())));
    const auto storage = static_cast<unsigned char *>(_shape.actors);
    Assert(_shape.bindings == reinterpret_cast<MMBodyPartBinding *>(storage + BodyBindingOffset(_Cbact())),
           "bad body binding storage");
    AssertPvCb(_Materials(), LwMul(_shape.set_count, size(PCustomMaterial_CMTL)));
    Assert(pvNil == _PbactRoot()->model, "Body root shouldn't have a model!!");
    Assert(pvNil == _PbactRoot()->material, "Body root shouldn't have a material!!");
    Assert(_Sets() && _Sets()->view.count == _shape.part_count, "bad body part set list");

    if (grf & fobjAssertFull)
    {
        for (ibact = 0; ibact < _shape.part_count; ibact++)
        {
            pbact = _PbactPart(ibact);
            if (pvNil != pbact->model)
                AssertPo(Model::PmodlFromBmdl(pbact->model), 0);
            if (pvNil != pbact->material)
                AssertPo(Material_MTRL::PmtrlFromBmtl(pbact->material), 0);
        }
        for (ibset = 0; ibset < _shape.set_count; ibset++)
        {
            AssertNilOrPo(_Materials()[ibset], 0);
        }
    }
}

/***************************************************************************
    Mark memory used by the Body
***************************************************************************/
void Body::MarkMem(void)
{
    AssertThis(0);

    LegacyLong ibact;
    PModel pmodl;
    LegacyLong ibset;
    bool fMtrl;
    PMaterial_MTRL pmtrl;
    PCustomMaterial_CMTL pcmtl;

    Body_PAR::MarkMem();
    MarkPv(_Actors());
    MarkPv(_Materials());
    if (_Sets())
        MarkMemObj(static_cast<BASE *>(_Sets()->owner));

    for (ibact = 0; ibact < _shape.part_count; ibact++)
    {
        pmodl = static_cast<PModel>(_shape.bindings[ibact+2].model_owner);
        if (pmodl)
        {
            AssertPo(pmodl, 0);
            MarkMemObj(pmodl);
        }
    }
    for (ibset = 0; ibset < _shape.set_count; ibset++)
    {
        GetPartSetMaterial(ibset, &fMtrl, &pmtrl, &pcmtl);
        if (fMtrl)
            MarkMemObj(pmtrl);
        else
            MarkMemObj(pcmtl);
    }
}
#endif // DEBUG

/***************************************************************************
    Create a blank costume -- no materials are attached yet
***************************************************************************/
BodyCostume::BodyCostume(void)
{
    mm_body_costume_init(&_costume);
}

/***************************************************************************
    Destroy a costume
***************************************************************************/
BodyCostume::~BodyCostume(void)
{
    AssertBaseThis(0);
    _Clear();
}

/***************************************************************************
    Release all arrays and references
***************************************************************************/
void BodyCostume::_Clear(void)
{
    AssertBaseThis(0);
    auto s = Body::_RuntimeServices(); mm_body_costume_clear(&_costume, &s);
}

/***************************************************************************
    Get a costume from a Body
***************************************************************************/
bool BodyCostume::FGet(Body *pbody)
{
    AssertThis(0); AssertPo(pbody, 0);
    auto s = Body::_RuntimeServices();
    const bool ok = mm_body_costume_capture(&_costume, pbody, &s) != 0;
    AssertThis(0); return ok;
}

/***************************************************************************
    Set a costume onto a Body.  The flag fAllowDifferentShape should usually
    be fFalse; it should be fTrue in the rare cases where it is appropriate
    to apply a costume with one number of body part sets to a Body with
    a (possibly) different number of body part sets.  In that case, the
    smaller number of materials are copied.  For example, when changing
    the number of characters in a 3-D Text object, the code grabs the
    current costume, resizes the ThreeDText and Body, sets the ThreeDText's Body to the
    default costume, then restores the old costume to the Body as much as
    is appropriate.
***************************************************************************/
void BodyCostume::Set(PBody pbody, bool fAllowDifferentShape)
{
    AssertThis(0); AssertPo(pbody, 0);
    auto s = Body::_RuntimeServices(); mm_body_costume_apply(&_costume, pbody, fAllowDifferentShape, &s);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the BodyCostume.
***************************************************************************/
void BodyCostume::AssertValid(LegacyUlong grf)
{
    LegacyLong ibset;
    BASE *po;

    if (pvNil != _costume.entries)
    {
        AssertPvCb(_costume.entries, LwMul(size(BASE *), _costume.count));
        for (ibset = 0; ibset < _costume.count; ibset++)
        {
            po = static_cast<BASE *>(_costume.entries[ibset]);
            if (po->FIs(kclsMaterial_MTRL))
                AssertPo((PMaterial_MTRL)po, 0);
            else
                AssertPo((PCustomMaterial_CMTL)po, 0);
        }
    }
}

/***************************************************************************
    Mark memory used by the BodyCostume
***************************************************************************/
void BodyCostume::MarkMem(void)
{
    AssertThis(0);

    LegacyLong ibset;

    if (pvNil != _costume.entries)
    {
        MarkPv(_costume.entries);
        for (ibset = 0; ibset < _costume.count; ibset++)
            MarkMemObj(static_cast<BASE *>(_costume.entries[ibset]));
    }
}

#endif DEBUG
