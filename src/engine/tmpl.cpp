/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tmpl.cpp: Actor template class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    Here's the Template chunk tree (* means more than one chunk can go here):

    Template - template flags
     |
     +--GLPI (chid 0) - parent IDs (Body part hierarchy)
     |
     +--GLBS (chid 0) - body part sets for Body
     |
     +--GGCM (chid 0) - custom costumes per body part set (GeneralGroup of cmids)
     |
     +--CustomMaterial_CMTL* (chid <cmid>) - custom material...see mtrl.h
     |   |
     |   +--Material_MTRL*
     |       |
     |       +--TextureMap
     |
     +--Model* - models used in this template
     |
     +--ACTN (chid <anid>) - action for this template
         |
         +--GGCL (chid 0) - GeneralGroup of cels for this action
         |
         +--GLXF (chid 0) - DynamicArray of transformation matrices for this action
         |
         +--GLMS (chid 0) - DynamicArray of motionmatch sounds for cels of this action

    About Actions: An action is an activity that a body can perform, such
    as walking, jumping, breathing, or resting.  Actions are broken down
    into cels, where each cel describes the position of each body part of
    the actor at one step or phase of the action.  After reaching the last
    cel, the action loops around to the beginning cel and repeats.  Cels
    consist of a list of cel part specs (CelPartSpec).  Each CelPartSpec refers to a single
    body part of an actor, such as a leg or head.  The CelPartSpec tells what
    BRender model to use for the body part for this cel and what matrix to
    use to orient it to its parent body part.  Each time the cel number is
    changed, Template reads each CelPartSpec of the new cel and updates each body part
    to use the new model and transformation matrix.

    About the GGCM: it is indexed by body part set, and tells how many
    custom materials are available for each body part set, and what the
    CMIDs are for those materials.  The first CMID in each body part set
    is the default one (for the default costume).  Example: If a body had
    3 body part sets (shirt, pants, and head), and there are 2 shirts,
    3 pants, and 1 head, the GGCM would be:

    0: fixed = 2, variable = (0, 1)
    1: fixed = 3, variable = (2, 3, 4)
    2: fixed = 1, variable = (5)

    where the fixed part is the count of available CMIDs for each body
    part set, and the numbers in the variable part are the CMIDs for the
    respective body part sets.  The default costume for this actor would
    be CMID 0 for the shirt, 2 for the pants, and 5 for the head.

***************************************************************************/
#include "soc_core.h"
#include "mm/movie_session.h"
ASSERTNAME

#ifdef DEBUG
#define TEMPLATE_VALIDATE(owner) EngineTemplate::Validate(owner)
#else
#define TEMPLATE_VALIDATE(owner) ((void)0)
#endif

MMTemplateCreateServices EngineTemplate::CreateServices()
{
    MMTemplateCreateServices s = {};
    mm_brender_template_read_services(&s, nullptr);
    s.create_body = [](void *, const MMPartIndexView *parents, const MMPartIndexView *sets) -> void * {
        return mm_brender_body_create(mm_kauai_body_renderer(), parents, sets);
    };
    s.release_body = [](void *, void *body) { mm_body_owner_release(static_cast<PBody>(body)); };
    s.default_costume = [](void *, void *object, void *body) -> int { return FSetDefaultCost(static_cast<PTemplate>(object), static_cast<PBody>(body)); };
    s.costume_id = [](void *, void *object, MMI32 set, MMI32 index) -> MMI32 { return CmidOfBset(static_cast<PTemplate>(object), set, index); };
    s.custom = [](void *, void *object, MMI32 id) -> void * { return PcmtlFetch(static_cast<PTemplate>(object), id); };
    s.allocate_refs = [](void *, MMI32 count, void **out) -> int { return FAllocPv(out, LwMul(count, size(PCustomMaterial_CMTL)), fmemClear, mprNormal); };
    s.apply_custom = [](void *, void *body, void *custom) { mm_body_set_custom(body, custom, &mm_kauai_body_renderer()->edit.runtime); };
    s.free_memory = [](void *, void **data) { FreePpv(data); };
    return s;
}

PTemplate EngineTemplate::Fetch(MMResourceCache *cache, ChunkTagOrType type, ChunkNumber number, bool *error)
{
    int failed = 0;
    auto owner = mm_brender_template_fetch(cache, type, number, &failed);
    if (error)
        *error = failed != 0;
    return owner;
}

bool EngineTemplate::WriteHeader(PTemplate owner, PChunkyFile file, ChunkTagOrType type, ChunkNumber *number)
{
    TEMPLATE_VALIDATE(owner); AssertPo(file, 0); AssertVarMem(number);
    const auto writer = file->WriteIndex();
    byte wire[kcbMaxDataStn];
    String name;
    GetName(owner, &name);
    const auto encoded = ChunkyFile::EncodeName(&name, wire);
    return mm_template_write_header(&State(owner), &encoded, type, number, &writer) != 0;
}

/***************************************************************************
    Creates a new tree of body parts (br_actors) based on this template.
    Note: Actor also calls FSetDefaultCost after creating the body, but
    by calling it here, it is guaranteed that the body will have a material
    on each body part (no null pointers for bact->material).  So the user
    will never see a body part that isn't texture mapped.
***************************************************************************/
PBody EngineTemplate::PbodyCreate(PTemplate owner)
{
    TEMPLATE_VALIDATE(owner);
    auto s = CreateServices();
    return static_cast<PBody>(mm_template_create_body(owner, &s));
}

/***************************************************************************
    Fills in the name of the given action
***************************************************************************/
static MMTemplateRuntimeServices RuntimeServices(PTemplate owner)
{
    MMTemplateRuntimeServices s = {}; s.storage = EngineTemplate::CreateServices();
    static_assert(sizeof(BRS) == sizeof(MMI32) && sizeof(BMAT34) == 12 * sizeof(MMI32), "fixed-point ABI");
    s.resource = mm_template_owner_binding(owner);
    s.model_reader = mm_brender_model_read;
    s.model_objects = mm_model_owner_services();
    s.name = [](void *, void *cache, MMU32 type, MMU32 number, void *out) -> int {
        return ReadResourceName(static_cast<MMResourceCache *>(cache), type, number, static_cast<PString>(out));
    };
    mm_template_use_action_resource(&s, EngineTemplate::ReadActionResource);
    const auto provider = mm_template_owner_provider(owner);
    if (mm_template_owner_is_text(owner) || provider->action)
    {
        s.generated_action = [](void *, void *object, MMI32 id) -> void * {
            auto owner = static_cast<PTemplate>(object);
            if (mm_template_owner_is_text(owner))
                return EngineText::FetchAction(owner, id);
            const auto provider = mm_template_owner_provider(owner);
            return provider->action(provider->context, id);
        };
    }
    if (mm_template_owner_is_text(owner) || provider->model)
    {
        s.generated_model = [](void *, void *object, MMU32 id, MMMovieAsset *asset) -> void * {
            auto owner = static_cast<PTemplate>(object);
            const auto provider = mm_template_owner_provider(owner);
            auto model = mm_template_owner_is_text(owner) ? EngineText::FetchModel(owner, id) :
                         static_cast<PModel>(provider->model(provider->context, id));
            if (model)
                *asset = {0, MM_TEMPLATE_BMDL, mm_model_owner_binding(model)->number};
            return model;
        };
    }
    s.sound = [](void *, void *action, MMI32 index, void *out) -> int {
        auto tag = static_cast<PTAG>(out);
        MMTag value;
        const auto definition = mm_action_resource_definition(static_cast<MMActionResource *>(action));
        tag->ctg = ctgNil;
        if (mm_action_cel_sound(definition, index, &value))
            TagFromCore(tag, value);
        return tag->ctg != ctgNil;
    };
    s.allocate_cel = [](void *, size_t bytes, void **out) -> int {
        return FAllocPv(out, static_cast<LegacyLong>(bytes), fmemClear, mprNormal);
    };
    s.set_model = [](void *, void *body, MMI32 part, void *model) { mm_body_set_model(body, part, model, &mm_kauai_body_renderer()->edit.runtime); };
    s.set_matrix = [](void *, void *body, MMI32 part, const MMActorMatrix *data) {
        mm_body_set_matrix(body, part, data, &mm_kauai_body_renderer()->edit.runtime);
    };
    s.sound_exists = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    return s;
}

bool EngineTemplate::FGetActnName(PTemplate owner, LegacyLong anid, PString pstn)
{
    TEMPLATE_VALIDATE(owner); AssertIn(anid, 0, State(owner).action_count); AssertPo(pstn, 0);
    auto s = RuntimeServices(owner);
    if (mm_template_owner_is_text(owner))
        return EngineText::ActionName(owner, anid, pstn);
    return mm_template_action_name(anid, pstn, &s) != 0;
}

/***************************************************************************
    Sets up the body part tree to use the correct models and transformation
    matrices for the given cel of the given action.  Also returns the
    distance to the next cel in *pdwr.
***************************************************************************/
bool EngineTemplate::FSetActnCel(PTemplate owner, PBody pbody, LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    TEMPLATE_VALIDATE(owner); Assert(pbody, "missing body"); AssertIn(anid, 0, State(owner).action_count); AssertNilOrVarMem(pdwr);
    auto s = RuntimeServices(owner);
    return mm_template_set_cel(owner, pbody, anid, celn, pdwr, &s) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool EngineTemplate::FGetDwrActnCel(PTemplate owner, LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    TEMPLATE_VALIDATE(owner); AssertIn(anid, 0, State(owner).action_count); AssertVarMem(pdwr);
    auto s = RuntimeServices(owner);
    MMI32 distance;
    if (!mm_template_distance(owner, anid, celn, &distance, &s)) return false;
    std::memcpy(pdwr, &distance, sizeof(distance)); return true;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool EngineTemplate::FGetCcelActn(PTemplate owner, LegacyLong anid, LegacyLong *pccel)
{
    TEMPLATE_VALIDATE(owner); AssertIn(anid, 0, State(owner).action_count); AssertVarMem(pccel);
    auto s = RuntimeServices(owner);
    return mm_template_cel_count(owner, anid, pccel, &s) != 0;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool EngineTemplate::FGetSndActnCel(PTemplate owner, LegacyLong anid, LegacyLong celn, bool *pfSoundExists, PTAG ptag)
{
    TEMPLATE_VALIDATE(owner); AssertIn(anid, 0, State(owner).action_count); AssertVarMem(pfSoundExists); AssertVarMem(ptag);
    auto s = RuntimeServices(owner);
    return mm_template_sound(owner, anid, celn, pfSoundExists, ptag, &s) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool EngineTemplate::FGetGrfactn(PTemplate owner, LegacyLong anid, LegacyUlong *pgrfactn)
{
    TEMPLATE_VALIDATE(owner); AssertIn(anid, 0, State(owner).action_count); AssertVarMem(pgrfactn);
    auto s = RuntimeServices(owner);
    return mm_template_action_flags(owner, anid, pgrfactn, &s) != 0;
}

/***************************************************************************
    Get orientation for template when actor has no path
***************************************************************************/
void EngineTemplate::GetRestOrien(PTemplate owner, BRA *pxa, BRA *pya, BRA *pza)
{
    TEMPLATE_VALIDATE(owner);
    AssertVarMem(pxa);
    AssertVarMem(pya);
    AssertVarMem(pza);

    *pxa = static_cast<BRA>(State(owner).x);
    *pya = static_cast<BRA>(State(owner).y);
    *pza = static_cast<BRA>(State(owner).z);
}

/***************************************************************************
    Puts default costume on pbody
***************************************************************************/
bool EngineTemplate::FSetDefaultCost(PTemplate owner, PBody pbody)
{
    TEMPLATE_VALIDATE(owner); Assert(pbody, "missing body");
    if (mm_template_owner_is_text(owner))
        return EngineText::FSetDefaultCost(owner, pbody);
    auto s = CreateServices();
    return mm_template_default_costume(owner, pbody, &s) != 0;
}

/***************************************************************************
    Returns the number of custom materials available for ibset
***************************************************************************/
LegacyLong EngineTemplate::CcmidOfBset(PTemplate owner, LegacyLong ibset)
{
    TEMPLATE_VALIDATE(owner); AssertIn(ibset, 0, State(owner).set_count);
    auto s = RuntimeServices(owner);
    return mm_template_costume_count(owner, ibset, &s);
}

/***************************************************************************
    Returns the icmid'th CMID available for ibset
***************************************************************************/
LegacyLong EngineTemplate::CmidOfBset(PTemplate owner, LegacyLong ibset, LegacyLong icmid)
{
    TEMPLATE_VALIDATE(owner); AssertIn(ibset, 0, State(owner).set_count); AssertIn(icmid, 0, CcmidOfBset(owner, ibset));
    auto s = RuntimeServices(owner);
    return mm_template_costume_id(owner, ibset, icmid, &s);
}

/***************************************************************************
    Tells whether ibset holds accessories by checking to see if one of
    its costumes has model children.
***************************************************************************/
bool EngineTemplate::FBsetIsAccessory(PTemplate owner, LegacyLong ibset)
{
    TEMPLATE_VALIDATE(owner); AssertIn(ibset, 0, State(owner).set_count);
    auto s = RuntimeServices(owner);
    return mm_template_is_accessory(owner, ibset, &s) != 0;
}

/***************************************************************************
    Returns the ibset of the accessory associated with ibset, if any.  If
    ibset is itself an accessory, it is returned in *pibsetAcc.  Otherwise,
    if ibset is the parent of an accessory, that accessory is returned in
    *pibsetAcc.
***************************************************************************/
bool EngineTemplate::FIbsetAccOfIbset(PTemplate owner, LegacyLong ibset, LegacyLong *pibsetAcc)
{
    TEMPLATE_VALIDATE(owner); AssertIn(ibset, 0, State(owner).set_count); AssertVarMem(pibsetAcc);
    auto s = RuntimeServices(owner);
    return mm_template_accessory_set(owner, ibset, pibsetAcc, &s) != 0;
}

/***************************************************************************
    See if cmid1 and cmid2 are for the same accessory by comparing child
    model chunks
***************************************************************************/
bool EngineTemplate::FSameAccCmids(PTemplate owner, LegacyLong cmid1, LegacyLong cmid2)
{
    TEMPLATE_VALIDATE(owner);
    auto s = RuntimeServices(owner);
    return mm_template_same_accessory(cmid1, cmid2, &s) != 0;
}

/***************************************************************************
    Get a custom material.  The cmid is really the chid under the Template.
***************************************************************************/
PCustomMaterial_CMTL EngineTemplate::PcmtlFetch(PTemplate owner, LegacyLong cmid)
{
    TEMPLATE_VALIDATE(owner);
    if (mm_template_owner_is_text(owner))
    {
        AssertIn(cmid, 0, State(owner).custom_count);
        Bug("Shouldn't fetch CMTLs from a ThreeDText");
        return pvNil;
    }
    // A CMID is a child ID, not an ordinal. Imported templates may have gaps
    // (for example, children 0 and 2), so State(owner).custom_count is not an upper bound on IDs.
    if (cmid < 0)
        return pvNil;
    return static_cast<PCustomMaterial_CMTL>(mm_template_fetch_child(
        mm_template_owner_binding(owner), static_cast<MMU32>(cmid), MM_TEMPLATE_CUSTOM,
        mm_brender_costume_read, mm_material_owner_services(), nullptr));
}

/***************************************************************************
    Puts the template's name into pstn
***************************************************************************/
void EngineTemplate::GetName(PTemplate owner, PString pstn)
{
    TEMPLATE_VALIDATE(owner);
    AssertPo(pstn, 0);

    pstn->SetSt(static_cast<PPascalString>(mm_template_owner_name(owner).buffer));
}

#ifdef DEBUG
void EngineTemplate::Validate(PTemplate owner)
{
    mm_kauai_template_validate(owner);
}

void EngineTemplate::Mark(PTemplate owner)
{
    mm_kauai_template_mark(owner);
}
#endif // DEBUG
