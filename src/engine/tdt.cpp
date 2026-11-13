/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdt.cpp: Three-D Text class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    ThreeDText, the 3-D Text class, is a derived class of Template.  Most clients
    (Actor, Movie, etc) can treat ThreeDTexts like regular Templates.  But they have
    some extra functionality and work internally very differently from
    Templates.  Chunkwise, all the information for a ThreeDText is in the Template
    chunk or the single ThreeDText child chunk:

    Template // template info
     |
     |
     +---ThreeDText  (chid 0) // ThreeDText-specific info (shape and tag to ThreeDFont)

    In addition to the usual Template fields, ThreeDTexts have a _tagTdf and a _tdts.
    _tagTdf tells what font to use for the ThreeDText, and _tdts tells what shape
    to draw the ThreeDText in.  ThreeDTexts on file are very small, so it is practical to
    store them in the user's document.

    Rather than fetching ActionDefinitions and the default costume from child chunks
    of the Template, ThreeDTexts generate them in memory and store them in _generated.action
    and _generated.material.  _generated.action keeps a copy of the last requested
    action so that it doesn't have to be continuously recomputed.

    The user can change a ThreeDText's text, shape, and/or font with FChange().
    When this happens, all the internal lists affecting the ThreeDText's shape,
    costume, etc., are changed via _FInitLists().  After changing a ThreeDText,
    you should call FAdjustBody on any Bodys based on that ThreeDText.  In
    Socrates, there should only be one Body per ThreeDText, so this shouldn't be
    a problem.

***************************************************************************/
#include "soc_core.h"
ASSERTNAME

RTCLASS(ThreeDText)

const ChildChunkID kchidTdt = 0; // ChildChunkID of ThreeDText under Template chunk


struct TextResourceContext { DataBlock block; String saved; };
static void TextTagToCore(const TAG *tag, ThreeMMTag *out)
{
    out->sid = tag->sid; std::memcpy(&out->cache, &tag->pcrf, sizeof(out->cache)); out->type = tag->ctg; out->number = tag->cno;
}
static void TextTagFromCore(TAG *tag, const ThreeMMTag *in)
{
    tag->sid = in->sid; std::memcpy(&tag->pcrf, &in->cache, sizeof(tag->pcrf)); tag->ctg = in->type; tag->cno = in->number;
}
ThreeMMTextResourceServices ThreeDText::_ResourceServices(void *context)
{
    ThreeMMTextResourceServices s = {}; s.context = context; s.system = koskCur; s.generation = _GenerationServices();
    static_assert(sizeof(LegacyLong) == 4 && sizeof(ChunkNumber) == 4 && sizeof(TAG) == sizeof(ThreeMMTag), "text resource ABI");
    s.state = [](void *, void *object) { return &static_cast<PThreeDText>(object)->_descriptor; };
    s.create = [](void *) -> void * { return NewObj ThreeDText; };
    s.text = [](void *, void *object) -> void * { return &static_cast<PThreeDText>(object)->_stn; };
    s.assign = [](void *, void *to, const void *from) { *static_cast<PString>(to) = *const_cast<PString>(static_cast<const String *>(from)); };
    s.space = [](void *, void *text) { achar ch = ChLit(' '); static_cast<PString>(text)->SetRgch(&ch, 1); };
    s.clear = [](void *, void *text) { static_cast<PString>(text)->SetNil(); };
    s.saved_text = [](void *p) -> void * { return &static_cast<TextResourceContext *>(p)->saved; };
    s.provider_copy = [](void *, void *to, void *from) {
        auto source = static_cast<PThreeDText>(from); static_cast<PThreeDText>(to)->SetFontResolver(source->_fontContext, source->_fontResolver);
    };
    s.has_provider = [](void *, void *object) -> int { return static_cast<PThreeDText>(object)->_fontResolver != nullptr; };
    s.provider_font = [](void *, void *object, const ThreeMMTag *in) -> void * {
        TAG tag; TextTagFromCore(&tag, in); auto t = static_cast<PThreeDText>(object); return t->_fontResolver(t->_fontContext, tag);
    };
    s.default_font = [](void *, const ThreeMMTag *in) -> void * {
        TAG tag; TextTagFromCore(&tag, in); return vptagm == nullptr ? nullptr : vptagm->PbacoFetch(&tag, ThreeDFont::FReadTdf);
    };
    s.publish_tag = [](void *, void *out, const ThreeMMTag *in) { TextTagFromCore(static_cast<PTAG>(out), in); };
    s.read_template = [](void *, void *object, void *file, ThreeMMU32 type, ThreeMMU32 number) -> int {
        return static_cast<PThreeDText>(object)->_FReadTmplf(static_cast<PChunkyFile>(file), type, number);
    };
    s.write_template = [](void *, void *object, void *file, ThreeMMU32 type, void *number) -> int {
        return static_cast<PThreeDText>(object)->_FWriteTmplf(static_cast<PChunkyFile>(file), type, static_cast<ChunkNumber *>(number));
    };
    s.child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 *out) -> int {
        ChildChunkIdentification child; if (!static_cast<PChunkyFile>(file)->FGetKidChidCtg(type, number, kchidTdt, kctgTdt, &child)) return 0;
        *out = child.cki.cno; return 1;
    };
    s.find = [](void *p, void *file, ThreeMMU32 number) -> void * {
        auto block = &static_cast<TextResourceContext *>(p)->block;
        return static_cast<PChunkyFile>(file)->FFind(kctgTdt, number, block) ? block : nullptr;
    };
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.block_size = [](void *, void *block) -> ThreeMMI32 { return static_cast<PDataBlock>(block)->Cb(); };
    s.read = [](void *, void *block, void *data, ThreeMMI32 bytes) -> int { return static_cast<PDataBlock>(block)->FReadRgb(data, bytes, 0); };
    s.add = [](void *p, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 *out) -> void * {
        auto block = &static_cast<TextResourceContext *>(p)->block; ChunkNumber child;
        if (!static_cast<PChunkyFile>(file)->FAddChild(type, number, kchidTdt, 24, kctgTdt, &child, block)) return nullptr;
        *out = child; return block;
    };
    s.write = [](void *, void *block, const void *data, ThreeMMI32 bytes) -> int { return static_cast<PDataBlock>(block)->FWriteRgb(const_cast<void *>(data), bytes, 0); };
    s.delete_child = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 child) { static_cast<PChunkyFile>(file)->DeleteChild(type, number, kctgTdt, child, kchidTdt); };
    s.publish_error = [](void *, void *out, int error) { *static_cast<bool *>(out) = error != 0; };
    s.tag_list = [](void *) -> void * { return DynamicArray::PglNew(sizeof(TAG)); };
    s.append_tag = [](void *, void *list, const ThreeMMTag *in) -> int { TAG tag; TextTagFromCore(&tag, in); return static_cast<PDynamicArray>(list)->FAdd(&tag); };
    s.name_count = [](void *, void *table) -> ThreeMMI32 { return static_cast<PStringTable_GST>(table)->IvMac(); };
    s.name_id = [](void *, void *table, ThreeMMI32 i) -> ThreeMMI32 { LegacyLong id; static_cast<PStringTable_GST>(table)->GetExtra(i, &id); return id; };
    s.name_text = [](void *, void *table, ThreeMMI32 i, void *text) { static_cast<PStringTable_GST>(table)->GetStn(i, static_cast<PString>(text)); };
    s.missing_name = [](void *) { Warn("action name not found"); };
    s.bad_order = [](void *) { Assert(false, "bad ThreeDTextF"); };
    return s;
}

ThreeMMTextNames ThreeDText::_actionNames = {};

PThreeDFont ThreeDText::_FetchFont(void)
{
    TextResourceContext context; auto services = _ResourceServices(&context);
    return static_cast<PThreeDFont>(three_mm_text_font(this, &services));
}

/***************************************************************************
    Set the StringTable_GST of action names for ThreeDTexts
***************************************************************************/
bool ThreeDText::FSetActionNames(PStringTable_GST pgstAction)
{
    AssertPo(pgstAction, 0); Assert(_actionNames.table == pvNil, "you already set the action names");
    TextResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_text_set_names(&_actionNames, pgstAction, &services) != 0;
}

/***************************************************************************
    Return a list of all tags embedded in this ThreeDText.  Note that a
    return value of pvNil does not mean an error occurred, but simply that
    this ThreeDText has no embedded tags.

    Actually, as currently implemented, this function only returns pvNil
    if an error occurs.  The point is, look at *pfError, not the return
    value.
***************************************************************************/
PDynamicArray ThreeDText::PgltagFetch(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool *pfError)
{
    AssertPo(pcfl, 0); AssertVarMem(pfError);
    TextResourceContext context; auto services = _ResourceServices(&context);
    return static_cast<PDynamicArray>(three_mm_text_tags(pcfl, ctg, cno, pfError, &services));
}

/***************************************************************************
    Create a new ThreeDText
***************************************************************************/
PThreeDText ThreeDText::PtdtNew(PString pstn, LegacyLong tdts, PTAG ptagTdf)
{
    AssertPo(pstn, 0); AssertIn(tdts, 0, tdtsLim); AssertVarMem(ptagTdf);
    TextResourceContext context; auto services = _ResourceServices(&context); ThreeMMTag font;
    TextTagToCore(ptagTdf, &font);
    return static_cast<PThreeDText>(three_mm_text_create(pstn, tdts, &font, &services));
}

/***************************************************************************
    Read the generic Template info and the ThreeDText-specific info (tdts and tagTdf),
    then call _FInitLists to build the rest of the ThreeDText.
***************************************************************************/
bool ThreeDText::_FInit(PChunkyFile pcfl, ChunkTagOrType ctgTmpl, ChunkNumber cnoTmpl)
{
    AssertBaseThis(0); AssertPo(pcfl, 0);
    TextResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_text_read(this, pcfl, ctgTmpl, cnoTmpl, &services) != 0;
}

/***************************************************************************
    Initialize or reinitialize the ThreeDText data structures, using the current
    _stn and _tdts.  This function unwinds completely on failure (the
    ThreeDText's members are untouched).
***************************************************************************/
ThreeMMTextGenerationServices ThreeDText::_GenerationServices()
{
    ThreeMMTextGenerationServices s = {};
    static_assert(sizeof(BMAT34) == sizeof(ThreeMMActorMatrix) && sizeof(short) == 2, "text generation ABI");
    static_assert(krPi == 205887 && krTwoPi == 411774 && krThreePi == 617662 && krHalfPi == 102943 && BR_SCALAR(0.5 / PI) == 10430, "text constants");
    static_assert(tdtsLim == THREE_MM_TEXT_SHAPE_COUNT && tdaLim == THREE_MM_TEXT_ACTION_COUNT && tdtsCircleZ == 6 && tdaWalk == 13 && tdaStretch == 15, "text enums");
    s.state = [](void *, void *object) { return &static_cast<PThreeDText>(object)->_generated; };
    s.length = [](void *, void *object) -> ThreeMMI32 { return static_cast<PThreeDText>(object)->_stn.Cch(); };
    s.character = [](void *, void *object, ThreeMMI32 i) -> ThreeMMI32 { return static_cast<uchar>(static_cast<PThreeDText>(object)->_stn.Psz()[i]); };
    s.shape = [](void *, void *object) -> ThreeMMI32 { return static_cast<PThreeDText>(object)->_descriptor.shape; };
    s.template_read = [](void *, void *object, ThreeMMTemplateState *out) {
        auto t = static_cast<PThreeDText>(object);
        *out = {t->_pglibactPar, t->_pglibset, t->_pggcmid, t->_cbset, t->_ccmid, t->_cactn, t->_xaRest, t->_yaRest, t->_zaRest, t->_grftmpl};
    };
    s.template_write = [](void *, void *object, const ThreeMMTemplateState *in) {
        auto t = static_cast<PThreeDText>(object);
        t->_pglibactPar = static_cast<PDynamicArray>(in->parents); t->_pglibset = static_cast<PDynamicArray>(in->sets); t->_pggcmid = static_cast<PGeneralGroup>(in->costumes);
        t->_cbset = in->set_count; t->_ccmid = in->custom_count; t->_cactn = in->action_count;
        t->_xaRest = static_cast<BRA>(in->x); t->_yaRest = static_cast<BRA>(in->y); t->_zaRest = static_cast<BRA>(in->z); t->_grftmpl = in->flags;
    };
    s.list_new = [](void *, ThreeMMI32 bytes, ThreeMMI32 capacity) -> void * { return DynamicArray::PglNew(bytes, capacity); };
    s.list_count = [](void *, void *list, ThreeMMI32 count) { AssertDo(static_cast<PDynamicArray>(list)->FSetIvMac(count), "PglNew should have ensured space!"); };
    s.list_put = [](void *, void *list, ThreeMMI32 index, const void *data) { static_cast<PDynamicArray>(list)->Put(index, const_cast<void *>(data)); };
    s.group_new = [](void *, ThreeMMI32 fixed, ThreeMMI32 rows, ThreeMMI32 bytes) -> void * { return GeneralGroup::PggNew(fixed, rows, bytes); };
    s.group_add = [](void *, void *group, const void *fixed, ThreeMMI32 bytes, const void *data) -> int { return static_cast<PGeneralGroup>(group)->FAdd(bytes, nullptr, const_cast<void *>(data), const_cast<void *>(fixed)); };
    s.allocate = [](void *, ThreeMMI32 bytes, void **out) -> int { return FAllocPv(out, bytes, fmemClear, mprNormal); };
    s.free_memory = [](void *, void **out) { FreePpv(out); };
    s.material_new = [](void *) -> void * { return Material_MTRL::PmtrlNew(); };
    s.retain = [](void *, void *object) { static_cast<BASE *>(object)->AddRef(); };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.font = [](void *, void *object) -> void * { return static_cast<PThreeDText>(object)->_FetchFont(); };
    s.font_width = [](void *, void *font, ThreeMMI32 ch) -> ThreeMMI32 { return static_cast<PThreeDFont>(font)->DxrChar(ch); };
    s.font_height = [](void *, void *font, ThreeMMI32 ch) -> ThreeMMI32 { return static_cast<PThreeDFont>(font)->DyrChar(ch); };
    s.font_maximum = [](void *, void *font) -> ThreeMMI32 { return static_cast<PThreeDFont>(font)->DyrMax(); };
    s.glyph = [](void *, void *font, ThreeMMI32 ch) -> void * { return static_cast<PThreeDFont>(font)->PmodlFetch(ch); };
    s.action_new = [](void *, void *cels, void *matrices, ThreeMMU32 flags) -> void * { return ActionDefinition::PactnNew(static_cast<PGeneralGroup>(cels), static_cast<PDynamicArray>(matrices), flags); };
    s.body_shape = [](void *, void *body, void *parents, void *sets) -> int { return static_cast<PBody>(body)->FChangeShape(static_cast<PDynamicArray>(parents), static_cast<PDynamicArray>(sets)); };
    s.body_material = [](void *, void *body, ThreeMMI32 set, void *material) { static_cast<PBody>(body)->SetPartSetMtrl(set, static_cast<PMaterial_MTRL>(material)); };
    return s;
}

bool ThreeDText::_FInitLists(void)
{
    AssertBaseThis(0); AssertPo(&_stn, 0); AssertIn(_descriptor.shape, 0, tdtsLim);
    auto services = _GenerationServices();
    return three_mm_text_initialize_lists(this, &services) != 0;
}

/***************************************************************************
    Get the given action.  If we've built it before, use the cached copy.
    Else build the action, cache it, and return it.
***************************************************************************/
PActionDefinition ThreeDText::_PactnFetch(LegacyLong tda)
{
    AssertThis(0); AssertIn(tda, 0, tdaLim);
    auto services = _GenerationServices();
    return static_cast<PActionDefinition>(three_mm_text_fetch_action(this, tda, &services));
}

/***************************************************************************
    Build the given action
***************************************************************************/
PActionDefinition ThreeDText::_PactnBuild(LegacyLong tda)
{
    AssertThis(0); AssertIn(tda, 0, tdaLim);
    auto services = _GenerationServices();
    return static_cast<PActionDefinition>(three_mm_text_build_action(this, tda, &services));
}

/***************************************************************************
    Get the name of the given action
***************************************************************************/
bool ThreeDText::FGetActnName(LegacyLong anid, PString pstn)
{
    AssertThis(0); AssertIn(anid, 0, tdaLim); AssertPo(pstn, 0);
    TextResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_text_action_name(&_actionNames, anid, pstn, &services) != 0;
}

/***************************************************************************
    Fetch the given model for this ThreeDText (use the ThreeDText's current font)
***************************************************************************/
PModel ThreeDText::_PmodlFetch(ChildChunkID chidModl)
{
    AssertThis(0); AssertIn(chidModl, 0, _stn.Cch());
    auto services = _GenerationServices();
    return static_cast<PModel>(three_mm_text_fetch_model(this, chidModl, &services));
}

/***************************************************************************
    Build the BACT tree DynamicArray for Body creation.  ThreeDTexts all have the same
    body part tree: every part is a child of the root.
***************************************************************************/
PDynamicArray ThreeDText::_PglibactParBuild(void)
{
    AssertBaseThis(0);
    auto services = _GenerationServices();
    return static_cast<PDynamicArray>(three_mm_text_build_indices(this, 0, &services));
}

/***************************************************************************
    Build the body part set DynamicArray for Body creation.  For ThreeDTexts, all body parts
    belong to a single body part set
***************************************************************************/
PDynamicArray ThreeDText::_PglibsetBuild(void)
{
    AssertBaseThis(0);
    auto services = _GenerationServices();
    return static_cast<PDynamicArray>(three_mm_text_build_indices(this, 1, &services));
}

/***************************************************************************
    Build the costume GeneralGroup for Template creation.  For ThreeDTexts, the costume is
    simple: all body part sets get cmid 0.
***************************************************************************/
PGeneralGroup ThreeDText::_PggcmidBuild(void)
{
    AssertBaseThis(0);
    auto services = _GenerationServices();
    return static_cast<PGeneralGroup>(three_mm_text_build_costumes(&services));
}

/***************************************************************************
    Build a DynamicArray of matrices for the action
***************************************************************************/
PDynamicArray ThreeDText::_Pglbmat34Build(LegacyLong tda)
{
    AssertBaseThis(0); AssertIn(tda, 0, tdaLim);
    auto services = _GenerationServices();
    return static_cast<PDynamicArray>(three_mm_text_build_matrices(this, tda, &services));
}

/***************************************************************************
    Build a GeneralGroup of cels for the action
***************************************************************************/
PGeneralGroup ThreeDText::_PggcelBuild(LegacyLong tda)
{
    AssertBaseThis(0); AssertIn(tda, 0, tdaLim);
    auto services = _GenerationServices();
    return static_cast<PGeneralGroup>(three_mm_text_build_cels(this, tda, &services));
}

/***************************************************************************
    Destruct the ThreeDText
***************************************************************************/
ThreeDText::~ThreeDText(void)
{
    AssertBaseThis(0);
    auto services = _GenerationServices();
    three_mm_text_dispose(&_generated, &services);
}

/***************************************************************************
    Return a duplicate of this ThreeDText
***************************************************************************/
PThreeDText ThreeDText::PtdtDup(void)
{
    AssertThis(0);
    TextResourceContext context; auto services = _ResourceServices(&context);
    return static_cast<PThreeDText>(three_mm_text_duplicate(this, &services));
}

/***************************************************************************
    Change the text, shape, and/or font of the ThreeDText
***************************************************************************/
bool ThreeDText::FChange(PString pstn, LegacyLong tdts, PTAG ptagTdf)
{
    AssertThis(0); AssertNilOrPo(pstn, 0); AssertNilOrVarMem(ptagTdf);
    if (tdts != tdtsNil) AssertIn(tdts, 0, tdtsLim);
    TextResourceContext context; auto services = _ResourceServices(&context); ThreeMMTag font;
    if (ptagTdf) TextTagToCore(ptagTdf, &font);
    return three_mm_text_change(this, pstn, tdts, ptagTdf ? &font : nullptr, &services) != 0;
}

/***************************************************************************
    Get stats of this ThreeDText
***************************************************************************/
void ThreeDText::GetInfo(PString pstn, LegacyLong *ptdts, PTAG ptagTdf)
{
    AssertThis(0); AssertNilOrPo(pstn, 0); AssertNilOrVarMem(ptdts); AssertNilOrVarMem(ptagTdf);
    TextResourceContext context; auto services = _ResourceServices(&context);
    three_mm_text_info(this, pstn, ptdts, ptagTdf, &services);
}

/***************************************************************************
    Adjust the given body's shape, since its owning ThreeDText has changed
***************************************************************************/
bool ThreeDText::FAdjustBody(PBody pbody)
{
    AssertThis(0); AssertPo(pbody, 0);
    auto services = _GenerationServices();
    return three_mm_text_adjust_body(this, pbody, &services) != 0;
}

/***************************************************************************
    Set the default costume.  This always succeeds for ThreeDTexts because they
    keep the PMaterial_MTRL in memory.
***************************************************************************/
bool ThreeDText::FSetDefaultCost(PBody pbody)
{
    AssertThis(0); AssertPo(pbody, 0);
    auto services = _GenerationServices();
    return three_mm_text_default_costume(this, pbody, &services) != 0;
}

/***************************************************************************
    Get a custom material
***************************************************************************/
PCustomMaterial_CMTL ThreeDText::PcmtlFetch(LegacyLong cmid)
{
    AssertThis(0);
    AssertIn(cmid, 0, _ccmid);

    Bug("Shouldn't fetch CMTLs from a ThreeDText");
    return pvNil;
}

/***************************************************************************
    Write the ThreeDText out as a Template hierarchy.
***************************************************************************/
bool ThreeDText::FWrite(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    AssertThis(0); AssertPo(pcfl, 0); AssertVarMem(pcno);
    TextResourceContext context; auto services = _ResourceServices(&context);
    return three_mm_text_write(this, pcfl, ctg, pcno, &services) != 0;
}

/***************************************************************************
    Return the number of cels in the given action
***************************************************************************/
LegacyLong ThreeDText::_CcelOfTda(LegacyLong tda)
{
    AssertThis(0); AssertIn(tda, 0, tdaLim);
    return three_mm_text_cel_count(tda);
}

/***************************************************************************
    Apply some transformations to each character based on tda.	The
    transformation matrix is returned in pbmat34.  Some transformations are
    pre-applied and some are post-applied, depending on the desired effect.
***************************************************************************/
void ThreeDText::_ApplyAction(BMAT34 *pbmat34, LegacyLong tda, LegacyLong ich, LegacyLong ccel, LegacyLong icel, BRS xrChar, BRS dxrText)
{
    AssertThis(0); AssertVarMem(pbmat34); AssertIn(tda, 0, tdaLim); AssertIn(icel, 0, ccel);
    ThreeMMActorMatrix matrix; std::memcpy(&matrix, pbmat34, sizeof(matrix));
    three_mm_text_apply_action(&matrix, tda, ich, ccel, icel, xrChar, dxrText);
    std::memcpy(pbmat34, &matrix, sizeof(matrix));
}

/***************************************************************************
    Shape the text based on the given tdts. This function is called once
    per character in the ThreeDText.  pbmat34 receives the transformation matrix
    for the character.  xrChar is the position of the current character
    (you could also think of this as the distance of the center of this
    character from the origin of the ThreeDText).	dxrText is the width of the
    entire string.  dyr is the height of the font.
***************************************************************************/
void ThreeDText::_ApplyShape(BMAT34 *pbmat34, LegacyLong tdts, LegacyLong cch, LegacyLong ich, BRS xrChar, BRS dxrText, BRS yrChar, BRS dyrMax,
                      BRS dyrTotal)
{
    AssertThis(0); AssertVarMem(pbmat34); AssertIn(tdts, 0, tdtsLim); AssertIn(cch, 0, kcchMaxStn); AssertIn(ich, 0, cch);
    ThreeMMActorMatrix matrix; std::memcpy(&matrix, pbmat34, sizeof(matrix));
    three_mm_text_apply_shape(&matrix, tdts, cch, ich, xrChar, dxrText, yrChar, dyrMax, dyrTotal);
    std::memcpy(pbmat34, &matrix, sizeof(matrix));
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the ThreeDText.
***************************************************************************/
void ThreeDText::AssertValid(LegacyUlong grf)
{
    ThreeDText_PAR::AssertValid(fobjAllocated);
    AssertNilOrPo(static_cast<PStringTable_GST>(_actionNames.table), 0); // Localized action names are optional in headless hosts.
    Assert(_descriptor.font.type != ctgNil, "ThreeDText has bad _tagTdf");
    AssertPo(static_cast<PMaterial_MTRL>(_generated.material), 0);
    if (tdaNil != _generated.action_id)
    {
        AssertIn(_generated.action_id, 0, tdaLim);
        AssertPo(static_cast<PActionDefinition>(_generated.action), 0);
    }
    else
    {
        Assert(pvNil == _generated.action, "_generated.action_id is wrong");
    }
    AssertIn(_descriptor.shape, 0, tdtsLim);
}

/***************************************************************************
    Mark memory used by the ThreeDText
***************************************************************************/
void ThreeDText::MarkMem(void)
{
    AssertThis(0);

    ThreeDText_PAR::MarkMem();

    MarkMemObj(static_cast<PMaterial_MTRL>(_generated.material));
    if (tdaNil != _generated.action_id)
        MarkMemObj(static_cast<PActionDefinition>(_generated.action));
}

/***************************************************************************
    Mark memory used by the ThreeDText action StringTable_GST
***************************************************************************/
void ThreeDText::MarkActionNames(void)
{
    MarkMemObj(static_cast<PStringTable_GST>(_actionNames.table));
}

#endif // DEBUG
