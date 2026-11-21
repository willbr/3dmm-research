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
#include "handle_services.h"
#include "mm/movie_session.h"
#include <typeinfo>
ASSERTNAME

RTCLASS(Template)

const MMActionResourceHost &Template::ActionResourceHost()
{
    static const auto tables = CollectionAllocator();
    static const MMActionResourceHost host = {
        KauaiResourceObjectAllocator(), &tables, KauaiDecodeBlockHandle,
#ifdef DEBUG
        [](void *memory) { if (memory) MarkPv(memory); },
        [](void *) { Assert(false, "bad ActionChunkOnFile"); },
        [](void *, void *out, size_t bytes) { FillPb(out, static_cast<LegacyLong>(bytes), kbGarbage); }
#else
        nullptr, nullptr, nullptr
#endif
    };
    return host;
}

int Template::ReadActionResource(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                                 void **out, MMI32 *bytes)
{
    return mm_action_resource_read(cache, type, number, block, out, bytes, &ActionResourceHost());
}

/***************************************************************************
    A PFNRPO (chunky resource reader function) to read Template objects.
***************************************************************************/
MMTemplateCreateServices Template::_CreateServices(PChunkyResourceFile cache)
{
    MMTemplateCreateServices s = {}; s.context = cache;
    const unsigned short one = 1; s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
#ifdef DEBUG
    s.debug_checks = 1;
#endif
    s.state = [](void *, void *object) -> MMTemplateState * { return &static_cast<PTemplate>(object)->_template; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.release_custom = [](void *, void *custom) { mm_material_owner_release(static_cast<PCustomMaterial_CMTL>(custom)); };
    s.decode = KauaiDecodeBlockHandle;
    s.read_name = [](void *cache, void *object, MMU32 type, MMU32 number) -> int {
        return static_cast<PChunkyResourceFile>(cache)->FGetName(type, number, &static_cast<PTemplate>(object)->_stn);
    };
    s.create = [](void *, int text) -> void * { return text ? static_cast<PTemplate>(NewObj ThreeDText) : NewObj Template; };
    s.initialize = [](void *cache, void *object, MMU32 type, MMU32 number, MMOwnedBlock *block) -> int {
        return static_cast<PTemplate>(object)->_FInit(static_cast<PChunkyResourceFile>(cache), type, number, block);
    };
    s.object_size = [](void *, int text) -> MMI32 { return static_cast<MMI32>(text ? sizeof(ThreeDText) : sizeof(Template)); };
    s.invalidate = [](void *, void *out, size_t count) {
#ifdef DEBUG
        FillPb(out, static_cast<LegacyLong>(count), kbGarbage);
#else
        (void)out; (void)count;
#endif
    };
    s.table_allocator = CollectionAllocator();
    s.create_cache = [](void *cache) -> void * { return static_cast<PChunkyResourceFile>(cache)->PcrfNewSibling(0); };
    s.fetch_custom = [](void *, void *cache, MMU32 type, MMU32 number) -> void * { return EngineCostume::Fetch(static_cast<PChunkyResourceFile>(cache), type, number); };
    s.custom_set = [](void *, void *custom) -> MMI32 { return EngineCostume::Set(static_cast<PCustomMaterial_CMTL>(custom)); };
    s.append_costumes = [](void *, MMTemplateCostumeTable *table, const MMI32 *ids, MMI32 count) -> int {
        return mm_template_costumes_append(table, ids, count);
    };
    s.create_body = [](void *, const MMPartIndexView *parents, const MMPartIndexView *sets) -> void * {
        return EngineBody::Create(*parents, *sets);
    };
    s.release_body = [](void *, void *body) { mm_body_owner_release(static_cast<PBody>(body)); };
    s.default_costume = [](void *, void *object, void *body) -> int { return static_cast<PTemplate>(object)->FSetDefaultCost(static_cast<PBody>(body)); };
    s.costume_id = [](void *, void *object, MMI32 set, MMI32 index) -> MMI32 { return static_cast<PTemplate>(object)->CmidOfBset(set, index); };
    s.custom = [](void *, void *object, MMI32 id) -> void * { return static_cast<PTemplate>(object)->PcmtlFetch(id); };
    s.allocate_refs = [](void *, MMI32 count, void **out) -> int { return FAllocPv(out, LwMul(count, size(PCustomMaterial_CMTL)), fmemClear, mprNormal); };
    s.apply_custom = [](void *, void *body, void *custom) { EngineBody::SetPartSetCmtl(static_cast<PBody>(body), static_cast<PCustomMaterial_CMTL>(custom)); };
    s.free_memory = [](void *, void **data) { FreePpv(data); };
    s.diagnostic = [](void *, int code) {
        switch (code) {
        case MM_TEMPLATE_BAD_ORDER: Assert(false, "freaky tmplf!"); break;
        case MM_TEMPLATE_OBSOLETE: Warn("Obsolete GLDC structure...get rid of it"); break;
        case MM_TEMPLATE_BUILD_COSTUMES: Warn("missing GGCM...building one on the fly"); break;
        case MM_TEMPLATE_COSTUME_LIMIT: Assert(false, "legacy costume table exceeds 50 entries per set"); break;
        case MM_TEMPLATE_WRONG_SET: Assert(false, "ibset's don't match"); break;
        }
    };
    return s;
}

int Template::FReadTmpl(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
    void **out, MMI32 *bytes)
{
    const auto s = _CreateServices(static_cast<PChunkyResourceFile>(cache->owner));
    return mm_template_fetch(cache, type, number, block, out, bytes, &s);
}

/***************************************************************************
    Read a Template from a chunk
***************************************************************************/
bool Template::_FReadTmplf(PChunkyResourceFile cache, ChunkTagOrType ctg, ChunkNumber cno, MMOwnedBlock *block)
{
    AssertBaseThis(0);
    auto s = _CreateServices(cache);
    return mm_template_read_header(this, cache->CoreCache(), ctg, cno, block, &s) != 0;
}

/***************************************************************************
    Write the TemplateOnFile chunk.  Creates a new chunk and returns the ChunkNumber in pcno.

    Note: In Socrates, normal actor templates are read-only, but this
    function will get called for ThreeDTexts.
***************************************************************************/
bool Template::_FWriteTmplf(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    AssertThis(0); AssertPo(pcfl, 0); AssertVarMem(pcno);
    const auto writer = pcfl->WriteIndex();
    byte wire[kcbMaxDataStn];
    const auto name = ChunkyFile::EncodeName(&_stn, wire);
    return mm_template_write_header(&_template, &name, ctg, pcno, &writer) != 0;
}

/***************************************************************************
    Read a Template from a chunk
***************************************************************************/
bool Template::_FInit(PChunkyResourceFile cache, ChunkTagOrType ctg, ChunkNumber cno, MMOwnedBlock *block)
{
    AssertPo(cache, 0);
    auto s = _CreateServices(cache);
    return mm_template_initialize(this, cache->CoreCache(), ctg, cno, block, &s) != 0;
}

/***************************************************************************
    Clean up and delete template
***************************************************************************/
Template::~Template(void)
{
    AssertBaseThis(0);
    auto s = _CreateServices();
    mm_template_dispose(this, &s);
}

/***************************************************************************
    Creates a new tree of body parts (br_actors) based on this template.
    Note: Actor also calls FSetDefaultCost after creating the body, but
    by calling it here, it is guaranteed that the body will have a material
    on each body part (no null pointers for bact->material).  So the user
    will never see a body part that isn't texture mapped.
***************************************************************************/
PBody Template::PbodyCreate(void)
{
    AssertThis(0);
    auto s = _CreateServices();
    return static_cast<PBody>(mm_template_create_body(this, &s));
}

/***************************************************************************
    Fills in the name of the given action
***************************************************************************/
MMTemplateRuntimeServices Template::_RuntimeServices()
{
    MMTemplateRuntimeServices s = {}; s.storage = _CreateServices();
    static_assert(sizeof(BRS) == sizeof(MMI32) && sizeof(BMAT34) == 12 * sizeof(MMI32), "fixed-point ABI");
    s.resource = ResourceBinding();
    s.model_reader = EngineModel::Read;
    s.model_objects = mm_model_owner_services();
    s.name = [](void *, void *cache, MMU32 type, MMU32 number, void *out) -> int {
        return static_cast<PChunkyResourceFile>(cache)->FGetName(type, number, static_cast<PString>(out));
    };
    mm_template_use_action_resource(&s, ReadActionResource);
    // Derived templates can supply either provider, even when backed by an archive.
    if (typeid(*this) != typeid(Template))
    {
        s.generated_action = [](void *, void *object, MMI32 id) -> void * {
            return static_cast<PTemplate>(object)->_FetchAction(id);
        };
        s.generated_model = [](void *, void *object, MMU32 id, MMMovieAsset *asset) -> void * {
            auto model = static_cast<PTemplate>(object)->_PmodlFetch(id);
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
    s.set_model = [](void *, void *body, MMI32 part, void *model) { EngineBody::SetPartModel(static_cast<PBody>(body), part, static_cast<PModel>(model)); };
    s.set_matrix = [](void *, void *body, MMI32 part, const MMActorMatrix *data) {
        EngineBody::SetPartMatrix(static_cast<PBody>(body), part, *data);
    };
    s.sound_exists = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    return s;
}

bool Template::FGetActnName(LegacyLong anid, PString pstn)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertPo(pstn, 0);
    auto s = _RuntimeServices();
    return mm_template_action_name(anid, pstn, &s) != 0;
}

/***************************************************************************
    Resolve an archive-backed action in the template's cache.
***************************************************************************/
MMActionResource *Template::_FetchAction(LegacyLong anid)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count);
    return mm_template_fetch_action(ResourceBinding(), anid, ReadActionResource);
}

/***************************************************************************
    Reads a Model chunk from disk
***************************************************************************/
PModel Template::_PmodlFetch(ChildChunkID chidModl)
{
    AssertThis(0);
    return static_cast<PModel>(mm_template_fetch_child(
        ResourceBinding(), chidModl, MM_TEMPLATE_MODEL, EngineModel::Read, mm_model_owner_services(), nullptr));
}

/***************************************************************************
    Sets up the body part tree to use the correct models and transformation
    matrices for the given cel of the given action.  Also returns the
    distance to the next cel in *pdwr.
***************************************************************************/
bool Template::FSetActnCel(PBody pbody, LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    AssertThis(0); Assert(pbody, "missing body"); AssertIn(anid, 0, _template.action_count); AssertNilOrVarMem(pdwr);
    auto s = _RuntimeServices();
    return mm_template_set_cel(this, pbody, anid, celn, pdwr, &s) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool Template::FGetDwrActnCel(LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertVarMem(pdwr);
    auto s = _RuntimeServices();
    MMI32 distance;
    if (!mm_template_distance(this, anid, celn, &distance, &s)) return false;
    std::memcpy(pdwr, &distance, sizeof(distance)); return true;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool Template::FGetCcelActn(LegacyLong anid, LegacyLong *pccel)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertVarMem(pccel);
    auto s = _RuntimeServices();
    return mm_template_cel_count(this, anid, pccel, &s) != 0;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool Template::FGetSndActnCel(LegacyLong anid, LegacyLong celn, bool *pfSoundExists, PTAG ptag)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertVarMem(pfSoundExists); AssertVarMem(ptag);
    auto s = _RuntimeServices();
    return mm_template_sound(this, anid, celn, pfSoundExists, ptag, &s) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool Template::FGetGrfactn(LegacyLong anid, LegacyUlong *pgrfactn)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertVarMem(pgrfactn);
    auto s = _RuntimeServices();
    return mm_template_action_flags(this, anid, pgrfactn, &s) != 0;
}

/***************************************************************************
    Get orientation for template when actor has no path
***************************************************************************/
void Template::GetRestOrien(BRA *pxa, BRA *pya, BRA *pza)
{
    AssertThis(0);
    AssertVarMem(pxa);
    AssertVarMem(pya);
    AssertVarMem(pza);

    *pxa = static_cast<BRA>(_template.x);
    *pya = static_cast<BRA>(_template.y);
    *pza = static_cast<BRA>(_template.z);
}

/***************************************************************************
    Puts default costume on pbody
***************************************************************************/
bool Template::FSetDefaultCost(PBody pbody)
{
    AssertThis(0); Assert(pbody, "missing body");
    auto s = _CreateServices();
    return mm_template_default_costume(this, pbody, &s) != 0;
}

/***************************************************************************
    Returns the number of custom materials available for ibset
***************************************************************************/
LegacyLong Template::CcmidOfBset(LegacyLong ibset)
{
    AssertThis(0); AssertIn(ibset, 0, _template.set_count);
    auto s = _RuntimeServices();
    return mm_template_costume_count(this, ibset, &s);
}

/***************************************************************************
    Returns the icmid'th CMID available for ibset
***************************************************************************/
LegacyLong Template::CmidOfBset(LegacyLong ibset, LegacyLong icmid)
{
    AssertThis(0); AssertIn(ibset, 0, _template.set_count); AssertIn(icmid, 0, CcmidOfBset(ibset));
    auto s = _RuntimeServices();
    return mm_template_costume_id(this, ibset, icmid, &s);
}

/***************************************************************************
    Tells whether ibset holds accessories by checking to see if one of
    its costumes has model children.
***************************************************************************/
bool Template::FBsetIsAccessory(LegacyLong ibset)
{
    AssertThis(0); AssertIn(ibset, 0, _template.set_count);
    auto s = _RuntimeServices();
    return mm_template_is_accessory(this, ibset, &s) != 0;
}

/***************************************************************************
    Returns the ibset of the accessory associated with ibset, if any.  If
    ibset is itself an accessory, it is returned in *pibsetAcc.  Otherwise,
    if ibset is the parent of an accessory, that accessory is returned in
    *pibsetAcc.
***************************************************************************/
bool Template::FIbsetAccOfIbset(LegacyLong ibset, LegacyLong *pibsetAcc)
{
    AssertThis(0); AssertIn(ibset, 0, _template.set_count); AssertVarMem(pibsetAcc);
    auto s = _RuntimeServices();
    return mm_template_accessory_set(this, ibset, pibsetAcc, &s) != 0;
}

/***************************************************************************
    See if cmid1 and cmid2 are for the same accessory by comparing child
    model chunks
***************************************************************************/
bool Template::FSameAccCmids(LegacyLong cmid1, LegacyLong cmid2)
{
    AssertThis(0);
    auto s = _RuntimeServices();
    return mm_template_same_accessory(cmid1, cmid2, &s) != 0;
}

/***************************************************************************
    Get a custom material.  The cmid is really the chid under the Template.
***************************************************************************/
PCustomMaterial_CMTL Template::PcmtlFetch(LegacyLong cmid)
{
    AssertThis(0);
    // A CMID is a child ID, not an ordinal. Imported templates may have gaps
    // (for example, children 0 and 2), so _template.custom_count is not an upper bound on IDs.
    if (cmid < 0)
        return pvNil;
    return static_cast<PCustomMaterial_CMTL>(mm_template_fetch_child(
        ResourceBinding(), static_cast<MMU32>(cmid), MM_TEMPLATE_CUSTOM,
        EngineCostume::Read, mm_material_owner_services(), nullptr));
}

/***************************************************************************
    Puts the template's name into pstn
***************************************************************************/
void Template::GetName(PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);

    *pstn = _stn;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the Template.
***************************************************************************/
void Template::AssertValid(LegacyUlong grftmpl)
{
    Template_PAR::AssertValid(fobjAllocated);
    Assert(_template.parents && _template.sets, "missing Template part lists");
    AssertPo(&_stn, 0);
    Assert(_template.costumes && _template.costumes->rows == static_cast<MMU32>(_template.set_count),
           "Bad template costume table");
}

/***************************************************************************
    Mark memory used by the Template
***************************************************************************/
void Template::MarkMem(void)
{
    AssertThis(0);
    Template_PAR::MarkMem();
    MMPartIndexList *indices[] = {_template.parents, _template.sets};
    for (auto list : indices)
        if (list)
        {
            MarkPv(list);
            MarkPv(list->storage.buffers[0].data);
        }
    if (_template.costumes)
    {
        MarkPv(_template.costumes);
        MarkPv(_template.costumes->storage.buffers[0].data);
        MarkPv(_template.costumes->storage.buffers[1].data);
    }
}
#endif // DEBUG
