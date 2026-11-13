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
ASSERTNAME

RTCLASS(ActionDefinition)
RTCLASS(Template)

/***************************************************************************
    Create a new action
***************************************************************************/
struct ActionDefinitionContext { DataBlock block; };
ThreeMMActionDefinitionServices ActionDefinition::_Services(ActionDefinitionContext *context)
{
    ThreeMMActionDefinitionServices s = {}; s.context = context;
    const unsigned short one = 1; s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
    static_assert(sizeof(AnimationCel) == 8 && sizeof(CelPartSpec) == 4 && sizeof(BMAT34) == 48, "action ABI");
    s.state = [](void *, void *object) { return &static_cast<PActionDefinition>(object)->_action; };
    s.create = [](void *) -> void * { return NewObj ActionDefinition; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.duplicate = [](void *, void *table, int kind) -> void * {
        return kind == THREE_MM_ACTION_CELS ? static_cast<void *>(static_cast<PGeneralGroup>(table)->PggDup()) :
            static_cast<void *>(static_cast<PDynamicArray>(table)->PglDup());
    };
    s.find = [](void *p, void *file, ThreeMMU32 type, ThreeMMU32 number) -> void * {
        auto block = &static_cast<ActionDefinitionContext *>(p)->block;
        return static_cast<PChunkyFile>(file)->FFind(type, number, block) ? block : nullptr;
    };
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.block_size = [](void *, void *block, int packed) -> ThreeMMI32 { return static_cast<PDataBlock>(block)->Cb(packed != 0); };
    s.block_read = [](void *, void *block, void *out, size_t count) -> int { return static_cast<PDataBlock>(block)->FReadRgb(out, static_cast<LegacyLong>(count), 0); };
    s.child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 childType, ThreeMMU32 *out) -> int {
        ChildChunkIdentification child; if (!static_cast<PChunkyFile>(file)->FGetKidChidCtg(type, number, 0, childType, &child)) return false;
        *out = child.cki.cno; return true;
    };
    s.read_table = [](void *, void *block, int kind, int *other) -> void * {
        short order = kboCur; void *result;
        if (kind == THREE_MM_ACTION_CELS) result = GeneralGroup::PggRead(static_cast<PDataBlock>(block), &order);
        else result = DynamicArray::PglRead(static_cast<PDataBlock>(block), &order);
        *other = order == kboOther; return result;
    };
    s.count = [](void *, void *table) -> ThreeMMI32 { return static_cast<GroupBase *>(table)->IvMac(); };
    s.table_size = [](void *, void *table) -> ThreeMMI32 { return static_cast<GroupBase *>(table)->CbOnFile(); };
    s.fixed = [](void *, void *group, ThreeMMI32 index) { return static_cast<unsigned char *>(static_cast<PGeneralGroup>(group)->QvFixedGet(index)); };
    s.variable = [](void *, void *group, ThreeMMI32 index) { return static_cast<unsigned char *>(static_cast<PGeneralGroup>(group)->QvGet(index)); };
    s.variable_size = [](void *, void *group, ThreeMMI32 index) -> ThreeMMI32 { return static_cast<PGeneralGroup>(group)->Cb(index); };
    s.data = [](void *, void *list, ThreeMMI32 index) { return static_cast<unsigned char *>(static_cast<PDynamicArray>(list)->QvGet(index)); };
    s.validate_wire_sounds = [](void *, void *list) { Assert(static_cast<PDynamicArray>(list)->CbEntry() == sizeof(TAGOnFile), "kctgGlms entry size drift"); };
    s.create_sounds = [](void *, ThreeMMI32 count) -> void * { return DynamicArray::PglNew(sizeof(TAG), count); };
    s.append_sound = [](void *, void *list, const ThreeMMTag *tag) -> int { TAG native; TagFromCore(&native, *tag); return static_cast<PDynamicArray>(list)->FAdd(&native); };
    s.read_sound = [](void *, void *list, ThreeMMI32 index, ThreeMMTag *out) {
        TAG tag; static_cast<PDynamicArray>(list)->Get(index, &tag); *out = {tag.sid, tag.pcrf, tag.ctg, tag.cno};
    };
    s.clear_sound_type = [](void *, void *out) { static_cast<PTAG>(out)->ctg = ctgNil; };
    s.publish_sound = [](void *, void *out, const ThreeMMTag *tag) { TagFromCore(static_cast<PTAG>(out), *tag); };
    s.initialize = [](void *, void *object, void *file, ThreeMMU32 type, ThreeMMU32 number) -> int { return static_cast<PActionDefinition>(object)->_FInit(static_cast<PChunkyFile>(file), type, number); };
    s.object_size = [](void *) -> ThreeMMI32 { return sizeof(ActionDefinition); };
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

PActionDefinition ActionDefinition::PactnNew(PGeneralGroup pggcel, PDynamicArray pglbmat34, LegacyUlong grfactn)
{
    AssertPo(pggcel, 0); AssertPo(pglbmat34, 0);
    ActionDefinitionContext context; auto s = _Services(&context);
    return static_cast<PActionDefinition>(three_mm_action_create(pggcel, pglbmat34, grfactn, &s));
}

/***************************************************************************
    A PFNRPO (chunky resource reader function) to read an ActionDefinition from a file
***************************************************************************/
bool ActionDefinition::FReadActn(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0); AssertPo(pblck, 0); AssertNilOrVarMem(ppbaco); AssertVarMem(pcb);
    ActionDefinitionContext context; auto s = _Services(&context);
    return three_mm_action_fetch(pcrf->Pcfl(), ctg, cno, pblck, ppbaco, pcb, &s) != 0;
}

/***************************************************************************
    Read an ActionDefinition from a chunk
***************************************************************************/
bool ActionDefinition::_FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertPo(pcfl, 0);
    ActionDefinitionContext context; auto s = _Services(&context);
    return three_mm_action_initialize(&_action, pcfl, ctg, cno, &s) != 0;
}

/***************************************************************************
    Destructor
***************************************************************************/
ActionDefinition::~ActionDefinition(void)
{
    AssertBaseThis(0);
    ActionDefinitionContext context; auto s = _Services(&context);
    three_mm_action_dispose(&_action, &s);
}

/***************************************************************************
    Get an AnimationCel
***************************************************************************/
LegacyLong ActionDefinition::Ccel(void)
{
    ActionDefinitionContext context; auto s = _Services(&context);
    return three_mm_action_cel_count(&_action, &s);
}

void ActionDefinition::GetCel(LegacyLong icel, AnimationCel *pcel)
{
    AssertThis(0); AssertIn(icel, 0, Ccel()); AssertVarMem(pcel);
    ActionDefinitionContext context; auto s = _Services(&context);
    three_mm_action_get_cel(&_action, icel, pcel, &s);
}

/***************************************************************************
    Get a CelPartSpec
***************************************************************************/
void ActionDefinition::GetCps(LegacyLong icel, LegacyLong icps, CelPartSpec *pcps)
{
    AssertThis(0); AssertIn(icel, 0, Ccel());
    AssertIn(icps, 0, static_cast<PGeneralGroup>(_action.cels)->Cb(icel) / sizeof(CelPartSpec)); AssertVarMem(pcps);
    ActionDefinitionContext context; auto s = _Services(&context);
    three_mm_action_get_part(&_action, icel, icps, pcps, &s);
}

/***************************************************************************
    Get a sound for icel.  If there is no sound, ptag's ChunkTagOrType is set to
    ctgNil.
***************************************************************************/
void ActionDefinition::GetSnd(LegacyLong icel, PTAG ptag)
{
    AssertThis(0); AssertIn(icel, 0, Ccel()); AssertVarMem(ptag);
    ActionDefinitionContext context; auto s = _Services(&context);
    three_mm_action_get_sound(&_action, icel, ptag, &s);
}

/***************************************************************************
    Get a transformation matrix
***************************************************************************/
void ActionDefinition::GetMatrix(LegacyLong imat34, BMAT34 *pbmat34)
{
    AssertThis(0); AssertIn(imat34, 0, static_cast<PDynamicArray>(_action.matrices)->IvMac()); AssertVarMem(pbmat34);
    ActionDefinitionContext context; auto s = _Services(&context);
    three_mm_action_get_matrix(&_action, imat34, pbmat34, &s);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the ActionDefinition.
***************************************************************************/
void ActionDefinition::AssertValid(LegacyUlong grf)
{
    ActionDefinition_PAR::AssertValid(fobjAllocated);
    AssertPo(static_cast<PGeneralGroup>(_action.cels), 0);
    AssertPo(static_cast<PDynamicArray>(_action.matrices), 0);
    AssertNilOrPo(static_cast<PDynamicArray>(_action.sounds), 0);
}

/***************************************************************************
    Mark memory used by the ActionDefinition
***************************************************************************/
void ActionDefinition::MarkMem(void)
{
    AssertThis(0);
    ActionDefinition_PAR::MarkMem();
    MarkMemObj(static_cast<PGeneralGroup>(_action.cels));
    MarkMemObj(static_cast<PDynamicArray>(_action.matrices));
    MarkMemObj(static_cast<PDynamicArray>(_action.sounds));
}
#endif // DEBUG

/***************************************************************************
    A PFNRPO (chunky resource reader function) to read Template objects.
***************************************************************************/
struct TemplateCreateContext { DataBlock block; };

ThreeMMTemplateCreateServices Template::_CreateServices(TemplateCreateContext *context)
{
    ThreeMMTemplateCreateServices s = {}; s.context = context; s.system = koskCur;
    const unsigned short one = 1; s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
#ifdef DEBUG
    s.debug_checks = 1;
#endif
    s.read = [](void *, void *object, ThreeMMTemplateState *state) {
        auto t = static_cast<PTemplate>(object);
        *state = {t->_pglibactPar, t->_pglibset, t->_pggcmid, t->_cbset, t->_ccmid, t->_cactn,
                  t->_xaRest, t->_yaRest, t->_zaRest, t->_grftmpl};
    };
    s.write = [](void *, void *object, const ThreeMMTemplateState *state) {
        auto t = static_cast<PTemplate>(object);
        t->_pglibactPar = static_cast<PDynamicArray>(state->parents); t->_pglibset = static_cast<PDynamicArray>(state->sets);
        t->_pggcmid = static_cast<PGeneralGroup>(state->costumes); t->_cbset = state->set_count;
        t->_ccmid = state->custom_count; t->_cactn = state->action_count;
        t->_xaRest = static_cast<BRA>(state->x); t->_yaRest = static_cast<BRA>(state->y);
        t->_zaRest = static_cast<BRA>(state->z); t->_grftmpl = state->flags;
    };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 id, ThreeMMU32 childType, ThreeMMTemplateChild *out) -> int {
        ChildChunkIdentification child;
        if (!static_cast<PChunkyFile>(file)->FGetKidChidCtg(type, number, id, childType, &child)) return 0;
        *out = {child.cki.ctg, child.cki.cno, child.chid}; return 1;
    };
    s.enumerate = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 index, ThreeMMTemplateChild *out) -> int {
        ChildChunkIdentification child;
        if (!static_cast<PChunkyFile>(file)->FGetKid(type, number, index, &child)) return 0;
        *out = {child.cki.ctg, child.cki.cno, child.chid}; return 1;
    };
    s.find = [](void *p, void *file, ThreeMMU32 type, ThreeMMU32 number) -> void * {
        auto ctx = static_cast<TemplateCreateContext *>(p);
        return static_cast<PChunkyFile>(file)->FFind(type, number, &ctx->block) ? &ctx->block : nullptr;
    };
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.block_size = [](void *, void *block, int packed) -> ThreeMMI32 { return static_cast<PDataBlock>(block)->Cb(packed != 0); };
    s.block_read = [](void *, void *block, void *bytes, size_t count) -> int { return static_cast<PDataBlock>(block)->FReadRgb(bytes, static_cast<LegacyLong>(count), 0); };
    s.read_name = [](void *, void *object, void *file, ThreeMMU32 type, ThreeMMU32 number) -> int { return static_cast<PChunkyFile>(file)->FGetName(type, number, &static_cast<PTemplate>(object)->_stn); };
    s.add_chunk = [](void *, void *file, ThreeMMU32 type, const void *data, size_t count, ThreeMMU32 *out) -> int {
        ChunkNumber number; CopyPb(out, &number, sizeof(number));
        const bool ok = static_cast<PChunkyFile>(file)->FAddPv(const_cast<void *>(data), static_cast<LegacyLong>(count), type, &number);
        CopyPb(&number, out, sizeof(number)); return ok;
    };
    s.set_name = [](void *, void *object, void *file, ThreeMMU32 type, ThreeMMU32 number) -> int { return static_cast<PChunkyFile>(file)->FSetName(type, number, &static_cast<PTemplate>(object)->_stn); };
    s.delete_chunk = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number) { static_cast<PChunkyFile>(file)->Delete(type, number); };
    s.create = [](void *, int text) -> void * { return text ? static_cast<PTemplate>(NewObj ThreeDText) : NewObj Template; };
    s.initialize = [](void *, void *object, void *file, ThreeMMU32 type, ThreeMMU32 number) -> int { return static_cast<PTemplate>(object)->_FInit(static_cast<PChunkyFile>(file), type, number); };
    s.object_size = [](void *, int text) -> ThreeMMI32 { return static_cast<ThreeMMI32>(text ? sizeof(ThreeDText) : sizeof(Template)); };
    s.table_size = [](void *, void *table) -> ThreeMMI32 { return static_cast<GroupBase *>(table)->CbOnFile(); };
    s.invalidate = [](void *, void *out, size_t count) {
#ifdef DEBUG
        FillPb(out, static_cast<LegacyLong>(count), kbGarbage);
#else
        (void)out; (void)count;
#endif
    };
    s.header = [](void *, void *object, void *file, ThreeMMU32 type, ThreeMMU32 number) -> int { return static_cast<PTemplate>(object)->_FReadTmplf(static_cast<PChunkyFile>(file), type, number); };
    s.read_table = [](void *, void *block, int kind, int *other) -> void * {
        short order = kboCur; void *result;
        if (kind == THREE_MM_TEMPLATE_COSTUMES) result = GeneralGroup::PggRead(static_cast<PDataBlock>(block), &order);
        else result = DynamicArray::PglRead(static_cast<PDataBlock>(block), &order);
        *other = order == kboOther; return result;
    };
    s.validate_table = [](void *, void *table, int kind, ThreeMMI32 expected) {
        if (kind == THREE_MM_TEMPLATE_COSTUMES) {
            auto group = static_cast<PGeneralGroup>(table); Assert(group->CbFixed() == sizeof(int32_t), "Bad Template _pggcmid");
            Assert(group->IvMac() == expected, "Bad Template _pggcmid");
        } else Assert(static_cast<PDynamicArray>(table)->CbEntry() == sizeof(short), "Bad Template part list");
    };
    s.count = [](void *, void *list) -> ThreeMMI32 { return static_cast<PDynamicArray>(list)->IvMac(); };
    s.item = [](void *, void *list, ThreeMMI32 i) -> ThreeMMI32 { short value; static_cast<PDynamicArray>(list)->Get(i, &value); return value; };
    s.list_data = [](void *, void *list) -> unsigned char * { return static_cast<unsigned char *>(static_cast<PDynamicArray>(list)->QvGet(0)); };
    s.fixed_data = [](void *, void *group, ThreeMMI32 row) -> unsigned char * { return static_cast<unsigned char *>(static_cast<PGeneralGroup>(group)->QvFixedGet(row)); };
    s.variable_data = [](void *, void *group, ThreeMMI32 row) -> unsigned char * { return static_cast<unsigned char *>(static_cast<PGeneralGroup>(group)->QvGet(row)); };
    s.create_cache = [](void *, void *file) -> void * { return ChunkyResourceFile::PcrfNew(static_cast<PChunkyFile>(file), 0); };
    s.create_costumes = [](void *) -> void * { return GeneralGroup::PggNew(sizeof(int32_t)); };
    s.fetch_custom = [](void *, void *cache, ThreeMMU32 type, ThreeMMU32 number) -> void * { return static_cast<PChunkyResourceFile>(cache)->PbacoFetch(type, number, CustomMaterial_CMTL::FReadCmtl); };
    s.custom_set = [](void *, void *custom) -> ThreeMMI32 { return static_cast<PCustomMaterial_CMTL>(custom)->Ibset(); };
    s.append_costumes = [](void *, void *group, const ThreeMMI32 *ids, ThreeMMI32 count) -> int {
        int32_t fixed = count; return static_cast<PGeneralGroup>(group)->FAdd(count * sizeof(int32_t), nullptr, const_cast<ThreeMMI32 *>(ids), &fixed);
    };
    s.create_body = [](void *, void *parents, void *sets) -> void * { return Body::PbodyNew(static_cast<PDynamicArray>(parents), static_cast<PDynamicArray>(sets)); };
    s.default_costume = [](void *, void *object, void *body) -> int { return static_cast<PTemplate>(object)->FSetDefaultCost(static_cast<PBody>(body)); };
    s.costume_id = [](void *, void *object, ThreeMMI32 set, ThreeMMI32 index) -> ThreeMMI32 { return static_cast<PTemplate>(object)->CmidOfBset(set, index); };
    s.custom = [](void *, void *object, ThreeMMI32 id) -> void * { return static_cast<PTemplate>(object)->PcmtlFetch(id); };
    s.allocate_refs = [](void *, ThreeMMI32 count, void **out) -> int { return FAllocPv(out, LwMul(count, size(PCustomMaterial_CMTL)), fmemClear, mprNormal); };
    s.apply_custom = [](void *, void *body, void *custom) { static_cast<PBody>(body)->SetPartSetCmtl(static_cast<PCustomMaterial_CMTL>(custom)); };
    s.free_memory = [](void *, void **data) { FreePpv(data); };
    s.diagnostic = [](void *, int code) {
        switch (code) {
        case THREE_MM_TEMPLATE_BAD_ORDER: Assert(false, "freaky tmplf!"); break;
        case THREE_MM_TEMPLATE_OBSOLETE: Warn("Obsolete GLDC structure...get rid of it"); break;
        case THREE_MM_TEMPLATE_BUILD_COSTUMES: Warn("missing GGCM...building one on the fly"); break;
        case THREE_MM_TEMPLATE_COSTUME_LIMIT: Assert(false, "legacy costume table exceeds 50 entries per set"); break;
        case THREE_MM_TEMPLATE_WRONG_SET: Assert(false, "ibset's don't match"); break;
        }
    };
    return s;
}

bool Template::FReadTmpl(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0); AssertPo(pblck, 0); AssertNilOrVarMem(ppbaco); AssertVarMem(pcb);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    void *object; ThreeMMI32 bytes;
    if (ppbaco) CopyPb(ppbaco, &object, sizeof(object));
    const bool ok = three_mm_template_fetch(pcrf->Pcfl(), ctg, cno, pblck, ppbaco ? &object : nullptr, &bytes, &s) != 0;
    if (ppbaco) CopyPb(&object, ppbaco, sizeof(object));
    CopyPb(&bytes, pcb, sizeof(bytes));
    return ok;
}

/***************************************************************************
    Read a Template from a chunk
***************************************************************************/
bool Template::_FReadTmplf(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertBaseThis(0);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    return three_mm_template_read_header(this, pcfl, ctg, cno, &s) != 0;
}

/***************************************************************************
    Write the TemplateOnFile chunk.  Creates a new chunk and returns the ChunkNumber in pcno.

    Note: In Socrates, normal actor templates are read-only, but this
    function will get called for ThreeDTexts.
***************************************************************************/
bool Template::_FWriteTmplf(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    AssertThis(0); AssertPo(pcfl, 0); AssertVarMem(pcno);
    TemplateCreateContext context; auto s = _CreateServices(&context); ThreeMMU32 number;
    CopyPb(pcno, &number, sizeof(number));
    const bool ok = three_mm_template_write_header(this, pcfl, ctg, &number, &s) != 0;
    CopyPb(&number, pcno, sizeof(number)); return ok;
}

/***************************************************************************
    Read a Template from a chunk
***************************************************************************/
bool Template::_FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno)
{
    AssertPo(pcfl, 0);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    return three_mm_template_initialize(this, pcfl, ctg, cno, &s) != 0;
}

/***************************************************************************
    Clean up and delete template
***************************************************************************/
Template::~Template(void)
{
    AssertBaseThis(0);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    three_mm_template_dispose(this, &s);
}

/***************************************************************************
    Return a list of all tags embedded in this Template.  Note that a
    return value of pvNil does not mean an error occurred, but simply that
    this Template has no embedded tags.
***************************************************************************/
PDynamicArray Template::PgltagFetch(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool *pfError)
{
    AssertPo(pcfl, 0);
    AssertVarMem(pfError);

    ChildChunkIdentification kid;

    *pfError = fFalse;
    if (pcfl->FGetKidChidCtg(ctg, cno, 0, kctgTdt, &kid))
        return ThreeDText::PgltagFetch(pcfl, ctg, cno, pfError);
    else
        return pvNil; // standard Templates have no embedded tags
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
    return static_cast<PBody>(three_mm_template_create_body(this, &s));
}

/***************************************************************************
    Fills in the name of the given action
***************************************************************************/
ThreeMMTemplateRuntimeServices Template::_RuntimeServices(TemplateCreateContext *context)
{
    ThreeMMTemplateRuntimeServices s = {}; s.storage = _CreateServices(context);
    static_assert(sizeof(BRS) == sizeof(ThreeMMI32) && sizeof(BMAT34) == 12 * sizeof(ThreeMMI32), "fixed-point ABI");
    s.binding = [](void *, void *object, ThreeMMTemplateBinding *out) {
        auto t = static_cast<PTemplate>(object); auto cache = t->Pcrf();
        *out = {cache ? cache->Pcfl() : nullptr, cache, t->Ctg(), t->Cno()};
    };
    s.fetch = [](void *, void *cache, ThreeMMU32 type, ThreeMMU32 number, int kind) -> void * {
        auto reader = kind == THREE_MM_TEMPLATE_ACTION ? ActionDefinition::FReadActn :
            kind == THREE_MM_TEMPLATE_MODEL ? Model::FReadModl : CustomMaterial_CMTL::FReadCmtl;
        return static_cast<PChunkyResourceFile>(cache)->PbacoFetch(type, number, reader);
    };
    s.name = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, void *out) -> int {
        return static_cast<PChunkyFile>(file)->FGetName(type, number, static_cast<PString>(out));
    };
    s.action = [](void *, void *object, ThreeMMI32 id) -> void * { return static_cast<PTemplate>(object)->_PactnFetch(id); };
    s.model = [](void *, void *object, ThreeMMU32 id) -> void * { return static_cast<PTemplate>(object)->_PmodlFetch(id); };
    s.cel_count = [](void *, void *action) -> ThreeMMI32 { return static_cast<PActionDefinition>(action)->Ccel(); };
    s.distance = [](void *, void *action, ThreeMMI32 index) -> ThreeMMI32 {
        AnimationCel cel; static_cast<PActionDefinition>(action)->GetCel(index, &cel); return cel.dwr;
    };
    s.flags = [](void *, void *action) -> ThreeMMU32 { return static_cast<PActionDefinition>(action)->Grfactn(); };
    s.part = [](void *, void *action, ThreeMMI32 cel, ThreeMMI32 part, ThreeMMI32 *model, ThreeMMI32 *matrix) {
        CelPartSpec cps; static_cast<PActionDefinition>(action)->GetCps(cel, part, &cps); *model = cps.chidModl; *matrix = cps.imat34;
    };
    s.matrix = [](void *, void *action, ThreeMMI32 index, ThreeMMI32 *out) {
        BMAT34 matrix; static_cast<PActionDefinition>(action)->GetMatrix(index, &matrix); std::memcpy(out, &matrix, sizeof(matrix));
    };
    s.set_model = [](void *, void *body, ThreeMMI32 part, void *model) { static_cast<PBody>(body)->SetPartModel(part, static_cast<PModel>(model)); };
    s.set_matrix = [](void *, void *body, ThreeMMI32 part, const ThreeMMI32 *data) {
        BMAT34 matrix; std::memcpy(&matrix, data, sizeof(matrix)); static_cast<PBody>(body)->SetPartMatrix(part, &matrix);
    };
    s.sound = [](void *, void *action, ThreeMMI32 index, void *out) -> int {
        auto tag = static_cast<PTAG>(out); static_cast<PActionDefinition>(action)->GetSnd(index, tag); return tag->ctg != ctgNil;
    };
    s.sound_exists = [](void *, void *out, int value) { *static_cast<bool *>(out) = value != 0; };
    return s;
}

bool Template::FGetActnName(LegacyLong anid, PString pstn)
{
    AssertThis(0); AssertIn(anid, 0, _cactn); AssertPo(pstn, 0);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_action_name(this, anid, pstn, &s) != 0;
}

/***************************************************************************
    Reads an ActionDefinition chunk from disk
***************************************************************************/
PActionDefinition Template::_PactnFetch(LegacyLong anid)
{
    AssertThis(0); AssertIn(anid, 0, _cactn);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return static_cast<PActionDefinition>(three_mm_template_fetch_child(this, anid, THREE_MM_TEMPLATE_ACTION, &s));
}

/***************************************************************************
    Reads a Model chunk from disk
***************************************************************************/
PModel Template::_PmodlFetch(ChildChunkID chidModl)
{
    AssertThis(0);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return static_cast<PModel>(three_mm_template_fetch_child(this, chidModl, THREE_MM_TEMPLATE_MODEL, &s));
}

/***************************************************************************
    Sets up the body part tree to use the correct models and transformation
    matrices for the given cel of the given action.  Also returns the
    distance to the next cel in *pdwr.
***************************************************************************/
bool Template::FSetActnCel(Body *pbody, LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    AssertThis(0); AssertPo(pbody, 0); AssertIn(anid, 0, _cactn); AssertNilOrVarMem(pdwr);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_set_cel(this, pbody, anid, celn, pdwr, &s) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool Template::FGetDwrActnCel(LegacyLong anid, LegacyLong celn, BRS *pdwr)
{
    AssertThis(0); AssertIn(anid, 0, _cactn); AssertVarMem(pdwr);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    ThreeMMI32 distance;
    if (!three_mm_template_distance(this, anid, celn, &distance, &s)) return false;
    std::memcpy(pdwr, &distance, sizeof(distance)); return true;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool Template::FGetCcelActn(LegacyLong anid, LegacyLong *pccel)
{
    AssertThis(0); AssertIn(anid, 0, _cactn); AssertVarMem(pccel);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_cel_count(this, anid, pccel, &s) != 0;
}

/***************************************************************************
    Retrieves the number of cels in this action
***************************************************************************/
bool Template::FGetSndActnCel(LegacyLong anid, LegacyLong celn, bool *pfSoundExists, PTAG ptag)
{
    AssertThis(0); AssertIn(anid, 0, _cactn); AssertVarMem(pfSoundExists); AssertVarMem(ptag);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_sound(this, anid, celn, pfSoundExists, ptag, &s) != 0;
}

/***************************************************************************
    Retrieves the distance travelled by cel celn of action anid.
***************************************************************************/
bool Template::FGetGrfactn(LegacyLong anid, LegacyUlong *pgrfactn)
{
    AssertThis(0); AssertIn(anid, 0, _cactn); AssertVarMem(pgrfactn);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_action_flags(this, anid, pgrfactn, &s) != 0;
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

    *pxa = _xaRest;
    *pya = _yaRest;
    *pza = _zaRest;
}

/***************************************************************************
    Puts default costume on pbody
***************************************************************************/
bool Template::FSetDefaultCost(Body *pbody)
{
    AssertThis(0); AssertPo(pbody, 0);
    TemplateCreateContext context; auto s = _CreateServices(&context);
    return three_mm_template_default_costume(this, pbody, &s) != 0;
}

/***************************************************************************
    Returns the number of custom materials available for ibset
***************************************************************************/
LegacyLong Template::CcmidOfBset(LegacyLong ibset)
{
    AssertThis(0); AssertIn(ibset, 0, _cbset);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_costume_count(this, ibset, &s);
}

/***************************************************************************
    Returns the icmid'th CMID available for ibset
***************************************************************************/
LegacyLong Template::CmidOfBset(LegacyLong ibset, LegacyLong icmid)
{
    AssertThis(0); AssertIn(ibset, 0, _cbset); AssertIn(icmid, 0, CcmidOfBset(ibset));
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_costume_id(this, ibset, icmid, &s);
}

/***************************************************************************
    Tells whether ibset holds accessories by checking to see if one of
    its costumes has model children.
***************************************************************************/
bool Template::FBsetIsAccessory(LegacyLong ibset)
{
    AssertThis(0); AssertIn(ibset, 0, _cbset);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_is_accessory(this, ibset, &s) != 0;
}

/***************************************************************************
    Returns the ibset of the accessory associated with ibset, if any.  If
    ibset is itself an accessory, it is returned in *pibsetAcc.  Otherwise,
    if ibset is the parent of an accessory, that accessory is returned in
    *pibsetAcc.
***************************************************************************/
bool Template::FIbsetAccOfIbset(LegacyLong ibset, LegacyLong *pibsetAcc)
{
    AssertThis(0); AssertIn(ibset, 0, _cbset); AssertVarMem(pibsetAcc);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_accessory_set(this, ibset, pibsetAcc, &s) != 0;
}

/***************************************************************************
    See if cmid1 and cmid2 are for the same accessory by comparing child
    model chunks
***************************************************************************/
bool Template::FSameAccCmids(LegacyLong cmid1, LegacyLong cmid2)
{
    AssertThis(0);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return three_mm_template_same_accessory(this, cmid1, cmid2, &s) != 0;
}

/***************************************************************************
    Get a custom material.  The cmid is really the chid under the Template.
***************************************************************************/
PCustomMaterial_CMTL Template::PcmtlFetch(LegacyLong cmid)
{
    AssertThis(0); AssertIn(cmid, 0, _ccmid);
    TemplateCreateContext context; auto s = _RuntimeServices(&context);
    return static_cast<PCustomMaterial_CMTL>(three_mm_template_fetch_child(this, cmid, THREE_MM_TEMPLATE_CUSTOM, &s));
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
    LegacyLong ibset;
    LegacyLong ccmid;

    Template_PAR::AssertValid(fobjAllocated);
    AssertPo(_pglibactPar, 0);
    AssertPo(_pglibset, 0);
    AssertPo(_pggcmid, 0);
    AssertPo(&_stn, 0);

    // Verify correctness of _pggcmid
    Assert(_pggcmid->IvMac() == _cbset, 0);
    for (ibset = 0; ibset < _cbset; ibset++)
    {
        ccmid = *(int32_t *)_pggcmid->QvFixedGet(ibset);
        Assert(_pggcmid->Cb(ibset) / size(int32_t) == ccmid, 0);
    }
}

/***************************************************************************
    Mark memory used by the Template
***************************************************************************/
void Template::MarkMem(void)
{
    AssertThis(0);
    Template_PAR::MarkMem();
    MarkMemObj(_pglibactPar);
    MarkMemObj(_pglibset);
    MarkMemObj(_pggcmid);
}
#endif // DEBUG
