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
     +--ActionDefinition* (chid <anid>) - action for this template
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
#include "engine_template_costumes.h"
#include "engine_part_indices.h"
#include "mm/movie_session.h"
#include "engine_read_stream.h"
ASSERTNAME

RTCLASS(ActionDefinition)
RTCLASS(Template)

/***************************************************************************
    Create a new action
***************************************************************************/
struct ActionDefinitionContext { DataBlock block; };

namespace
{
class ActionTableAllocation : public BASE
{
  public:
    MMActionTable table;

    ActionTableAllocation()
    {
        const auto allocator = CollectionAllocator();
        mm_action_table_init(&table, &allocator, this,
                             [](void *owner) { static_cast<ActionTableAllocation *>(owner)->Release(); });
    }

#ifdef DEBUG
    void MarkMem() override
    {
        BASE::MarkMem();
        MarkPv(table.storage.buffers[0].data);
        MarkPv(table.storage.buffers[1].data);
    }
#endif
};
}

MMActionTable *ActionDefinition::NewTable()
{
    const auto allocation = NewObj ActionTableAllocation;
    return allocation ? &allocation->table : nullptr;
}

MMActionDefinitionServices ActionDefinition::_Services(ActionDefinitionContext *context)
{
    MMActionDefinitionServices s = {}; s.context = context;
    const unsigned short one = 1; s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
    static_assert(sizeof(AnimationCel) == 8 && sizeof(CelPartSpec) == 4 && sizeof(BMAT34) == 48, "action ABI");
    s.state = [](void *, void *object) { return &static_cast<PActionDefinition>(object)->_action; };
    s.create = [](void *) -> void * { return NewObj ActionDefinition; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.new_table = [](void *, int) { return NewTable(); };
    s.find = [](void *p, void *cache, MMU32 type, MMU32 number) -> void * {
        auto block = &static_cast<ActionDefinitionContext *>(p)->block;
        return static_cast<PChunkyResourceFile>(cache)->FReadBlock(type, number, *block) ? block : nullptr;
    };
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.block_size = [](void *, void *block, int packed) -> MMI32 { return static_cast<PDataBlock>(block)->Cb(packed != 0); };
    s.block_read = [](void *, void *block, void *out, size_t count) -> int { return static_cast<PDataBlock>(block)->FReadRgb(out, static_cast<LegacyLong>(count), 0); };
    s.child = [](void *, void *cache, MMU32 type, MMU32 number, MMU32 childType, MMU32 *out) -> int {
        const auto chunks = static_cast<PChunkyResourceFile>(cache)->ReadChunkAccess();
        return mm_chunk_link_number(&chunks, type, number, 0, childType, out) == MM_CHUNK_OK;
    };
    s.stream = [](void *, void *block) {
        return EngineReadStream::FromBlock(*static_cast<PDataBlock>(block));
    };
    s.clear_sound_type = [](void *, void *out) { static_cast<PTAG>(out)->ctg = ctgNil; };
    s.publish_sound = [](void *, void *out, const MMTag *tag) { TagFromCore(static_cast<PTAG>(out), *tag); };
    s.object_size = [](void *) -> MMI32 { return sizeof(ActionDefinition); };
    s.invalidate = [](void *, void *out, size_t count) {
#ifdef DEBUG
        FillPb(out, static_cast<LegacyLong>(count), kbGarbage);
#else
        (void)out; (void)count;
#endif
    };
    s.bad_order = [](void *) { Assert(false, "bad ActionChunkOnFile"); };
    return s;
}

PActionDefinition ActionDefinition::PactnNew(MMActionTable *cels, MMActionTable *matrices, LegacyUlong grfactn)
{
    Assert(cels && cels->header.kind == MM_COLLECTION_GROUP && cels->header.width == MM_ACTION_CEL_BYTES,
           "bad animation cel table");
    Assert(matrices && matrices->header.kind == MM_COLLECTION_ARRAY && matrices->header.width == MM_ACTION_MATRIX_BYTES,
           "bad action matrix table");
    ActionDefinitionContext context; auto s = _Services(&context);
    return static_cast<PActionDefinition>(mm_action_create(cels, matrices, grfactn, &s));
}

/***************************************************************************
    A PFNRPO (chunky resource reader function) to read an ActionDefinition from a file
***************************************************************************/
bool ActionDefinition::FReadActn(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0); AssertPo(pblck, 0); AssertNilOrVarMem(ppbaco); AssertVarMem(pcb);
    ActionDefinitionContext context; auto s = _Services(&context);
    return mm_action_fetch(pcrf, ctg, cno, pblck, ppbaco, pcb, &s) != 0;
}

bool ActionDefinition::_FInit(PChunkyResourceFile cache, ChunkTagOrType type, ChunkNumber number)
{
    AssertPo(cache, 0);
    ActionDefinitionContext context;
    const auto services = _Services(&context);
    return mm_action_initialize(&_action, cache, type, number, nullptr, &services) != 0;
}

/***************************************************************************
    Destructor
***************************************************************************/
ActionDefinition::~ActionDefinition(void)
{
    AssertBaseThis(0);
    mm_action_dispose(&_action);
}

/***************************************************************************
    Get an AnimationCel
***************************************************************************/
LegacyLong ActionDefinition::Ccel(void)
{
    return mm_action_cel_count(&_action);
}

bool ActionDefinition::FGetCel(LegacyLong icel, AnimationCel *pcel)
{
    AssertThis(0); AssertVarMem(pcel);
    return mm_action_get_cel(&_action, icel, pcel) != 0;
}

/***************************************************************************
    Get a CelPartSpec
***************************************************************************/
bool ActionDefinition::FGetCps(LegacyLong icel, LegacyLong icps, CelPartSpec *pcps)
{
    AssertThis(0); AssertVarMem(pcps);
    return mm_action_get_part(&_action, icel, icps, pcps) != 0;
}

/***************************************************************************
    Get a sound for icel.  If there is no sound, ptag's ChunkTagOrType is set to
    ctgNil.
***************************************************************************/
void ActionDefinition::GetSnd(LegacyLong icel, PTAG ptag)
{
    AssertThis(0); AssertIn(icel, 0, Ccel()); AssertVarMem(ptag);
    ActionDefinitionContext context; auto s = _Services(&context);
    mm_action_get_sound(&_action, icel, ptag, &s);
}

/***************************************************************************
    Get a transformation matrix
***************************************************************************/
bool ActionDefinition::FGetMatrix(LegacyLong imat34, BMAT34 *pbmat34)
{
    AssertThis(0); AssertVarMem(pbmat34);
    return mm_action_get_matrix(&_action, imat34, pbmat34) != 0;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the ActionDefinition.
***************************************************************************/
void ActionDefinition::AssertValid(LegacyUlong grf)
{
    ActionDefinition_PAR::AssertValid(fobjAllocated);
    Assert(_action.cels && _action.matrices, "missing action tables");
}

/***************************************************************************
    Mark memory used by the ActionDefinition
***************************************************************************/
void ActionDefinition::MarkMem(void)
{
    AssertThis(0);
    ActionDefinition_PAR::MarkMem();
    for (auto table : {_action.cels, _action.matrices, _action.sounds})
        if (table && table->owner)
            MarkMemObj(static_cast<BASE *>(table->owner));
}
#endif // DEBUG

/***************************************************************************
    A PFNRPO (chunky resource reader function) to read Template objects.
***************************************************************************/
struct TemplateCreateContext { DataBlock block; };

MMTemplateCreateServices Template::_CreateServices(TemplateCreateContext *context)
{
    MMTemplateCreateServices s = {}; s.context = context; s.system = koskCur;
    const unsigned short one = 1; s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
#ifdef DEBUG
    s.debug_checks = 1;
#endif
    s.state = [](void *, void *object) -> MMTemplateState * { return &static_cast<PTemplate>(object)->_template; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.child = [](void *, void *cache, MMU32 type, MMU32 number, MMU32 id, MMU32 childType, MMTemplateChild *out) -> int {
        const auto chunks = static_cast<PChunkyResourceFile>(cache)->ReadChunkAccess();
        MMU32 child;
        if (mm_chunk_link_number(&chunks, type, number, id, childType, &child) != MM_CHUNK_OK)
            return 0;
        *out = {childType, child, id};
        return 1;
    };
    s.enumerate = [](void *, void *cache, MMU32 type, MMU32 number, MMI32 index, MMTemplateChild *out) -> int {
        const auto chunks = static_cast<PChunkyResourceFile>(cache)->ReadChunkAccess();
        MMU32 parent;
        MMChunkChild child;
        if (mm_chunk_find(&chunks, type, number, &parent) != MM_CHUNK_OK ||
            mm_chunk_child_at(&chunks, parent, index, &child) != MM_CHUNK_OK)
            return 0;
        *out = {child.type, child.number, child.slot};
        return 1;
    };
    s.find = [](void *p, void *cache, MMU32 type, MMU32 number) -> void * {
        auto ctx = static_cast<TemplateCreateContext *>(p);
        return static_cast<PChunkyResourceFile>(cache)->FReadBlock(type, number, ctx->block) ? &ctx->block : nullptr;
    };
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.block_size = [](void *, void *block, int packed) -> MMI32 { return static_cast<PDataBlock>(block)->Cb(packed != 0); };
    s.block_read = [](void *, void *block, void *bytes, size_t count) -> int { return static_cast<PDataBlock>(block)->FReadRgb(bytes, static_cast<LegacyLong>(count), 0); };
    s.read_name = [](void *, void *object, void *cache, MMU32 type, MMU32 number) -> int {
        return static_cast<PChunkyResourceFile>(cache)->Pcfl()->FGetName(type, number, &static_cast<PTemplate>(object)->_stn);
    };
    s.add_chunk = [](void *, void *file, MMU32 type, const void *data, size_t count, MMU32 *out) -> int {
        ChunkNumber number; CopyPb(out, &number, sizeof(number));
        const bool ok = static_cast<PChunkyFile>(file)->FAddPv(const_cast<void *>(data), static_cast<LegacyLong>(count), type, &number);
        CopyPb(&number, out, sizeof(number)); return ok;
    };
    s.set_name = [](void *, void *object, void *file, MMU32 type, MMU32 number) -> int { return static_cast<PChunkyFile>(file)->FSetName(type, number, &static_cast<PTemplate>(object)->_stn); };
    s.delete_chunk = [](void *, void *file, MMU32 type, MMU32 number) { static_cast<PChunkyFile>(file)->Delete(type, number); };
    s.create = [](void *, int text) -> void * { return text ? static_cast<PTemplate>(NewObj ThreeDText) : NewObj Template; };
    s.initialize = [](void *, void *object, void *cache, MMU32 type, MMU32 number, void *block) -> int {
        return static_cast<PTemplate>(object)->_FInit(static_cast<PChunkyResourceFile>(cache), type, number,
                                                     static_cast<PDataBlock>(block));
    };
    s.object_size = [](void *, int text) -> MMI32 { return static_cast<MMI32>(text ? sizeof(ThreeDText) : sizeof(Template)); };
    s.invalidate = [](void *, void *out, size_t count) {
#ifdef DEBUG
        FillPb(out, static_cast<LegacyLong>(count), kbGarbage);
#else
        (void)out; (void)count;
#endif
    };
    s.header = [](void *, void *object, void *cache, MMU32 type, MMU32 number, void *block) -> int {
        return static_cast<PTemplate>(object)->_FReadTmplf(static_cast<PChunkyResourceFile>(cache), type, number,
                                                          static_cast<PDataBlock>(block));
    };
    s.read_part_indices = [](void *, void *block, MMPartIndexList **out) -> int {
        auto list = EnginePartIndices::Create();
        if (!list)
            return MM_MOVIE_MEMORY;
        auto data = static_cast<PDataBlock>(block);
        int status = MM_MOVIE_BAD_FILE;
        if (data->FUnpackData())
        {
            const auto stream = EngineReadStream::FromBlock(*data);
            status = mm_part_indices_read(list, &stream);
        }
        if (status == MM_MOVIE_OK)
            *out = list;
        else
            mm_part_indices_dispose(list);
        return status;
    };
    s.read_costumes = [](void *, void *block, MMI32 rows, MMTemplateCostumeTable **out) -> int {
        auto table = EngineTemplateCostumes::Create();
        if (!table)
            return MM_MOVIE_MEMORY;
        auto data = static_cast<PDataBlock>(block);
        int status = MM_MOVIE_BAD_FILE;
        if (data->FUnpackData())
        {
            const auto stream = EngineReadStream::FromBlock(*data);
            status = mm_template_costumes_read(table, &stream, rows);
        }
        if (status == MM_MOVIE_OK)
            *out = table;
        else
            mm_template_costumes_dispose(table);
        return status;
    };
    s.create_cache = [](void *, void *cache) -> void * { return static_cast<PChunkyResourceFile>(cache)->PcrfNewSibling(0); };
    s.create_costumes = [](void *) { return EngineTemplateCostumes::Create(); };
    s.fetch_custom = [](void *, void *cache, MMU32 type, MMU32 number) -> void * { return static_cast<PChunkyResourceFile>(cache)->PbacoFetch(type, number, CustomMaterial_CMTL::FReadCmtl); };
    s.custom_set = [](void *, void *custom) -> MMI32 { return static_cast<PCustomMaterial_CMTL>(custom)->Ibset(); };
    s.append_costumes = [](void *, MMTemplateCostumeTable *table, const MMI32 *ids, MMI32 count) -> int {
        return mm_template_costumes_append(table, ids, count);
    };
    s.create_body = [](void *, const MMPartIndexView *parents, const MMPartIndexView *sets) -> void * {
        return Body::PbodyNew(*parents, *sets);
    };
    s.default_costume = [](void *, void *object, void *body) -> int { return static_cast<PTemplate>(object)->FSetDefaultCost(static_cast<PBody>(body)); };
    s.costume_id = [](void *, void *object, MMI32 set, MMI32 index) -> MMI32 { return static_cast<PTemplate>(object)->CmidOfBset(set, index); };
    s.custom = [](void *, void *object, MMI32 id) -> void * { return static_cast<PTemplate>(object)->PcmtlFetch(id); };
    s.allocate_refs = [](void *, MMI32 count, void **out) -> int { return FAllocPv(out, LwMul(count, size(PCustomMaterial_CMTL)), fmemClear, mprNormal); };
    s.apply_custom = [](void *, void *body, void *custom) { static_cast<PBody>(body)->SetPartSetCmtl(static_cast<PCustomMaterial_CMTL>(custom)); };
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

bool Template::FReadTmpl(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0); AssertPo(pblck, 0); AssertNilOrVarMem(ppbaco); AssertVarMem(pcb);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    void *object; MMI32 bytes;
    if (ppbaco) CopyPb(ppbaco, &object, sizeof(object));
    const bool ok = mm_template_fetch(pcrf, ctg, cno, pblck, ppbaco ? &object : nullptr, &bytes, &s) != 0;
    if (ppbaco) CopyPb(&object, ppbaco, sizeof(object));
    CopyPb(&bytes, pcb, sizeof(bytes));
    return ok;
}

/***************************************************************************
    Read a Template from a chunk
***************************************************************************/
bool Template::_FReadTmplf(PChunkyResourceFile cache, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock block)
{
    AssertBaseThis(0);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    return mm_template_read_header(this, cache, ctg, cno, block, &s) != 0;
}

/***************************************************************************
    Write the TemplateOnFile chunk.  Creates a new chunk and returns the ChunkNumber in pcno.

    Note: In Socrates, normal actor templates are read-only, but this
    function will get called for ThreeDTexts.
***************************************************************************/
bool Template::_FWriteTmplf(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    AssertThis(0); AssertPo(pcfl, 0); AssertVarMem(pcno);
    TemplateCreateContext context; auto s = _CreateServices(&context); MMU32 number;
    CopyPb(pcno, &number, sizeof(number));
    const bool ok = mm_template_write_header(this, pcfl, ctg, &number, &s) != 0;
    CopyPb(&number, pcno, sizeof(number)); return ok;
}

/***************************************************************************
    Read a Template from a chunk
***************************************************************************/
bool Template::_FInit(PChunkyResourceFile cache, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock block)
{
    AssertPo(cache, 0);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    return mm_template_initialize(this, cache, ctg, cno, block, &s) != 0;
}

/***************************************************************************
    Clean up and delete template
***************************************************************************/
Template::~Template(void)
{
    AssertBaseThis(0);
    TemplateCreateContext context; auto s = _CreateServices(&context);
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
    TemplateCreateContext context; auto s = _CreateServices(&context);
    return static_cast<PBody>(mm_template_create_body(this, &s));
}

/***************************************************************************
    Fills in the name of the given action
***************************************************************************/
MMTemplateRuntimeServices Template::_RuntimeServices(TemplateCreateContext *context)
{
    MMTemplateRuntimeServices s = {}; s.storage = _CreateServices(context);
    static_assert(sizeof(BRS) == sizeof(MMI32) && sizeof(BMAT34) == 12 * sizeof(MMI32), "fixed-point ABI");
    s.binding = [](void *, void *object, MMTemplateBinding *out) {
        auto t = static_cast<PTemplate>(object); auto cache = t->Pcrf();
        *out = {cache, t->Ctg(), t->Cno()};
    };
    s.fetch = [](void *, void *cache, MMU32 type, MMU32 number, int kind) -> void * {
        auto reader = kind == MM_TEMPLATE_ACTION ? ActionDefinition::FReadActn :
            kind == MM_TEMPLATE_MODEL ? Model::FReadModl : CustomMaterial_CMTL::FReadCmtl;
        return static_cast<PChunkyResourceFile>(cache)->PbacoFetch(type, number, reader);
    };
    s.name = [](void *, void *cache, MMU32 type, MMU32 number, void *out) -> int {
        return static_cast<PChunkyResourceFile>(cache)->Pcfl()->FGetName(type, number, static_cast<PString>(out));
    };
    s.action = [](void *, void *object, MMI32 id) -> void * { return static_cast<PTemplate>(object)->_PactnFetch(id); };
    s.model = [](void *, void *object, MMU32 id, MMMovieAsset *asset) -> void * {
        auto model = static_cast<PTemplate>(object)->_PmodlFetch(id);
        if (model)
            *asset = {0, MM_TEMPLATE_BMDL, model->Cno()};
        return model;
    };
    s.allocate_cel = [](void *, size_t bytes, void **out) -> int {
        return FAllocPv(out, static_cast<LegacyLong>(bytes), fmemClear, mprNormal);
    };
    s.cel_count = [](void *, void *action) -> MMI32 { return static_cast<PActionDefinition>(action)->Ccel(); };
    s.distance = [](void *, void *action, MMI32 index, MMI32 *out) -> int {
        AnimationCel cel;
        if (!static_cast<PActionDefinition>(action)->FGetCel(index, &cel))
            return 0;
        *out = cel.dwr;
        return 1;
    };
    s.flags = [](void *, void *action) -> MMU32 { return static_cast<PActionDefinition>(action)->Grfactn(); };
    s.part = [](void *, void *action, MMI32 cel, MMI32 part, MMI32 *model, MMI32 *matrix) -> int {
        CelPartSpec cps;
        if (!static_cast<PActionDefinition>(action)->FGetCps(cel, part, &cps))
            return 0;
        *model = cps.chidModl;
        *matrix = cps.imat34;
        return 1;
    };
    s.matrix = [](void *, void *action, MMI32 index, MMActorMatrix *out) -> int {
        BMAT34 matrix;
        if (!static_cast<PActionDefinition>(action)->FGetMatrix(index, &matrix))
            return 0;
        std::memcpy(out, &matrix, sizeof(matrix));
        return 1;
    };
    s.set_model = [](void *, void *body, MMI32 part, void *model) { static_cast<PBody>(body)->SetPartModel(part, static_cast<PModel>(model)); };
    s.set_matrix = [](void *, void *body, MMI32 part, const MMActorMatrix *data) {
        static_cast<PBody>(body)->SetPartMatrix(part, *data);
    };
    s.sound = [](void *, void *action, MMI32 index, void *out) -> int {
        auto tag = static_cast<PTAG>(out); static_cast<PActionDefinition>(action)->GetSnd(index, tag); return tag->ctg != ctgNil;
    };
    s.sound_exists = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    return s;
}

bool Template::FGetActnName(LegacyLong anid, PString pstn)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertPo(pstn, 0);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return mm_template_action_name(this, anid, pstn, &s) != 0;
}

/***************************************************************************
    Reads an ActionDefinition chunk from disk
***************************************************************************/
PActionDefinition Template::_PactnFetch(LegacyLong anid)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return static_cast<PActionDefinition>(mm_template_fetch_child(this, anid, MM_TEMPLATE_ACTION, &s));
}

/***************************************************************************
    Reads a Model chunk from disk
***************************************************************************/
PModel Template::_PmodlFetch(ChildChunkID chidModl)
{
    AssertThis(0);
    const auto cache = Pcrf();
    if (!cache)
        return nullptr;
    const auto graph = cache->ReadChunkAccess();
    MMU32 parent;
    MMMovieAsset asset;
    if (mm_chunk_find(&graph, Ctg(), Cno(), &parent) != MM_CHUNK_OK ||
        mm_movie_cel_model(&graph, parent, chidModl, 0, &asset) != MM_MOVIE_OK)
        return nullptr;
    return static_cast<PModel>(cache->PbacoFetch(asset.type, asset.number, Model::FReadModl));
}

/***************************************************************************
    Sets up the body part tree to use the correct models and transformation
    matrices for the given cel of the given action.  Also returns the
    distance to the next cel in *pdwr.
***************************************************************************/
bool Template::FSetActnCel(Body *pbody, LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    AssertThis(0); AssertPo(pbody, 0); AssertIn(anid, 0, _template.action_count); AssertNilOrVarMem(pdwr);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return mm_template_set_cel(this, pbody, anid, celn, pdwr, &s) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool Template::FGetDwrActnCel(LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertVarMem(pdwr);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
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
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return mm_template_cel_count(this, anid, pccel, &s) != 0;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool Template::FGetSndActnCel(LegacyLong anid, LegacyLong celn, bool *pfSoundExists, PTAG ptag)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertVarMem(pfSoundExists); AssertVarMem(ptag);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return mm_template_sound(this, anid, celn, pfSoundExists, ptag, &s) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool Template::FGetGrfactn(LegacyLong anid, LegacyUlong *pgrfactn)
{
    AssertThis(0); AssertIn(anid, 0, _template.action_count); AssertVarMem(pgrfactn);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
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
bool Template::FSetDefaultCost(Body *pbody)
{
    AssertThis(0); AssertPo(pbody, 0);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    return mm_template_default_costume(this, pbody, &s) != 0;
}

/***************************************************************************
    Returns the number of custom materials available for ibset
***************************************************************************/
LegacyLong Template::CcmidOfBset(LegacyLong ibset)
{
    AssertThis(0); AssertIn(ibset, 0, _template.set_count);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return mm_template_costume_count(this, ibset, &s);
}

/***************************************************************************
    Returns the icmid'th CMID available for ibset
***************************************************************************/
LegacyLong Template::CmidOfBset(LegacyLong ibset, LegacyLong icmid)
{
    AssertThis(0); AssertIn(ibset, 0, _template.set_count); AssertIn(icmid, 0, CcmidOfBset(ibset));
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return mm_template_costume_id(this, ibset, icmid, &s);
}

/***************************************************************************
    Tells whether ibset holds accessories by checking to see if one of
    its costumes has model children.
***************************************************************************/
bool Template::FBsetIsAccessory(LegacyLong ibset)
{
    AssertThis(0); AssertIn(ibset, 0, _template.set_count);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
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
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return mm_template_accessory_set(this, ibset, pibsetAcc, &s) != 0;
}

/***************************************************************************
    See if cmid1 and cmid2 are for the same accessory by comparing child
    model chunks
***************************************************************************/
bool Template::FSameAccCmids(LegacyLong cmid1, LegacyLong cmid2)
{
    AssertThis(0);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return mm_template_same_accessory(this, cmid1, cmid2, &s) != 0;
}

/***************************************************************************
    Get a custom material.  The cmid is really the chid under the Template.
***************************************************************************/
PCustomMaterial_CMTL Template::PcmtlFetch(LegacyLong cmid)
{
    AssertThis(0);
    // A CMID is a child ID, not an ordinal. Imported templates may have gaps
    // (for example, children 0 and 2), so _template.custom_count is not an upper bound on IDs.
    const auto cache = Pcrf();
    if (cmid < 0 || !cache)
        return pvNil;
    const auto graph = cache->ReadChunkAccess();
    MMU32 parent;
    MMChunkEntry costume;
    if (mm_chunk_find(&graph, Ctg(), Cno(), &parent) != MM_CHUNK_OK ||
        mm_movie_costume_link(&graph, parent, static_cast<MMU32>(cmid), &costume, nullptr) != MM_MOVIE_OK)
        return pvNil;
    return static_cast<PCustomMaterial_CMTL>(cache->PbacoFetch(costume.type, costume.number, CustomMaterial_CMTL::FReadCmtl));
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
    if (_template.parents)
        MarkMemObj(static_cast<BASE *>(_template.parents->owner));
    if (_template.sets)
        MarkMemObj(static_cast<BASE *>(_template.sets->owner));
    if (_template.costumes)
        MarkMemObj(static_cast<BASE *>(_template.costumes->owner));
}
#endif // DEBUG
