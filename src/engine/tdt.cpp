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


PStringTable_GST ThreeDText::_pgstAction = pvNil;

PThreeDFont ThreeDText::_FetchFont(void)
{
    if (_fontResolver != pvNil)
        return _fontResolver(_fontContext, _tagTdf);
    return vptagm == pvNil ? pvNil : (PThreeDFont)vptagm->PbacoFetch(&_tagTdf, ThreeDFont::FReadTdf);
}

/***************************************************************************
    Set the StringTable_GST of action names for ThreeDTexts
***************************************************************************/
bool ThreeDText::FSetActionNames(PStringTable_GST pgstAction)
{
    AssertPo(pgstAction, 0);
    Assert(pvNil == _pgstAction, "you already set the action names");

    _pgstAction = pgstAction;
    _pgstAction->AddRef();
    return fTrue;
}

/****************************************
    3-D Text On File...this gets put in
    a child chunk of a Template
****************************************/
struct ThreeDTextF
{
    int16_t bo;
    int16_t osk;
    int32_t tdts;
    TAGOnFile tagTdf;
};
static_assert(sizeof(ThreeDTextF) == 24, "ThreeDTextF on-disk layout drift");
const ByteOrderMask kbomTdtf = (0x5C000000 | kbomTag >> 6);

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
    AssertPo(pcfl, 0);
    AssertVarMem(pfError);

    PDynamicArray pgltag;
    ChildChunkIdentification kid;
    DataBlock blck;
    ThreeDTextF tdtf;

    *pfError = fFalse;
    pgltag = DynamicArray::PglNew(size(TAG));
    if (pvNil == pgltag)
        goto LFail;
    if (!pcfl->FGetKidChidCtg(ctg, cno, kchidTdt, kctgTdt, &kid))
        goto LFail;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        goto LFail;
    if (!blck.FUnpackData())
        goto LFail;
    if (blck.Cb() < size(ThreeDTextF))
        goto LFail;
    if (!blck.FReadRgb(&tdtf, size(ThreeDTextF), 0))
        goto LFail;
    if (kboCur != tdtf.bo)
        SwapBytesBom(&tdtf, kbomTdtf);
    Assert(kboCur == tdtf.bo, "bad ThreeDTextF");
    {
        TAG tagTdf;
        TagFromOnFile(&tagTdf, tdtf.tagTdf);
        if (!pgltag->FAdd(&tagTdf))
            goto LFail;
    }
    return pgltag;
LFail:
    *pfError = fTrue;
    ReleasePpo(&pgltag);
    return pvNil;
}

/***************************************************************************
    Create a new ThreeDText
***************************************************************************/
PThreeDText ThreeDText::PtdtNew(PString pstn, LegacyLong tdts, PTAG ptagTdf)
{
    AssertPo(pstn, 0);
    AssertIn(tdts, 0, tdtsLim);
    AssertVarMem(ptagTdf);

    PThreeDText ptdt;

    ptdt = NewObj ThreeDText;
    if (pvNil == ptdt)
        return pvNil;
    ptdt->_stn = *pstn;

    // This is a bit of a hack, but it makes life easier.  Without it,
    // the code would have to deal with body part sets with no body
    // parts in them, and Template and Body would have problems with that
    // (for one thing, they would compute _cbset incorrectly).
    if (ptdt->_stn.Cch() == 0)
    {
        achar chSpace = ChLit(' ');
        ptdt->_stn.SetRgch(&chSpace, 1);
    }

    ptdt->_tdts = tdts;
    ptdt->_tagTdf = *ptagTdf;
    if (!ptdt->_FInitLists())
    {
        ReleasePpo(&ptdt);
        return pvNil;
    }
    AssertPo(ptdt, 0);

    return ptdt;
}

/***************************************************************************
    Read the generic Template info and the ThreeDText-specific info (tdts and tagTdf),
    then call _FInitLists to build the rest of the ThreeDText.
***************************************************************************/
bool ThreeDText::_FInit(PChunkyFile pcfl, ChunkTagOrType ctgTmpl, ChunkNumber cnoTmpl)
{
    AssertBaseThis(0);
    AssertPo(pcfl, 0);

    ChildChunkIdentification kid;
    DataBlock blck;
    ThreeDTextF tdtf;

    if (!_FReadTmplf(pcfl, ctgTmpl, cnoTmpl))
        return fFalse;
    if (!pcfl->FGetKidChidCtg(ctgTmpl, cnoTmpl, kchidTdt, kctgTdt, &kid))
        return fFalse;
    if (!pcfl->FFind(kid.cki.ctg, kid.cki.cno, &blck))
        return fFalse;
    if (!blck.FUnpackData())
        return fFalse;
    if (blck.Cb() < size(ThreeDTextF))
        return fFalse;
    if (!blck.FReadRgb(&tdtf, size(ThreeDTextF), 0))
        return fFalse;
    if (kboCur != tdtf.bo)
        SwapBytesBom(&tdtf, kbomTdtf);
    Assert(kboCur == tdtf.bo, "bad ThreeDTextF");
    TagFromOnFile(&_tagTdf, tdtf.tagTdf);
    _tdts = tdtf.tdts;

    if (!_FInitLists())
        return fFalse;

    return fTrue;
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
    s.shape = [](void *, void *object) -> ThreeMMI32 { return static_cast<PThreeDText>(object)->_tdts; };
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
    AssertBaseThis(0); AssertPo(&_stn, 0); AssertIn(_tdts, 0, tdtsLim);
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
    AssertThis(0);
    AssertIn(anid, 0, tdaLim);
    AssertPo(pstn, 0);

    LegacyLong istn;
    LegacyLong anidT;

    if (pvNil == _pgstAction)
    {
        pstn->SetNil();
        return fFalse;
    }

    for (istn = 0; istn < _pgstAction->IvMac(); istn++)
    {
        _pgstAction->GetExtra(istn, &anidT);
        if (anid == anidT)
        {
            _pgstAction->GetStn(istn, pstn);
            return fTrue;
        }
    }
    Warn("action name not found");
    pstn->SetNil();
    return fFalse;
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

    PThreeDText ptdtDup;

    ptdtDup = ThreeDText::PtdtNew(&_stn, _tdts, &_tagTdf);
    if (pvNil != ptdtDup)
        ptdtDup->SetFontResolver(_fontContext, _fontResolver);
    AssertNilOrPo(ptdtDup, 0);

    return ptdtDup;
}

/***************************************************************************
    Change the text, shape, and/or font of the ThreeDText
***************************************************************************/
bool ThreeDText::FChange(PString pstn, LegacyLong tdts, PTAG ptagTdf)
{
    AssertThis(0);
    AssertNilOrPo(pstn, 0);
    if (tdtsNil != tdts)
        AssertIn(tdts, 0, tdtsLim);
    AssertNilOrVarMem(ptagTdf);

    String stnSave;
    LegacyLong tdtsSave;
    TAG tagTdfSave;

    stnSave = _stn;
    tdtsSave = _tdts;
    tagTdfSave = _tagTdf;

    if (pvNil != pstn)
        _stn = *pstn;
    if (tdtsNil != tdts)
        _tdts = tdts;
    if (pvNil != ptagTdf)
        _tagTdf = *ptagTdf;

    // This is a bit of a hack, but it makes life easier.  Without it,
    // the code would have to deal with body part sets with no body
    // parts in them, and Template and Body would have problems with that
    // (for one thing, they would compute _cbset incorrectly).
    if (_stn.Cch() == 0)
    {
        achar chSpace = ChLit(' ');
        _stn.SetRgch(&chSpace, 1);
    }

    if (!_FInitLists()) // note: unwinds on failure
    {
        _stn = stnSave;
        _tdts = tdtsSave;
        _tagTdf = tagTdfSave;
        return fFalse;
    }

    return fTrue;
}

/***************************************************************************
    Get stats of this ThreeDText
***************************************************************************/
void ThreeDText::GetInfo(PString pstn, LegacyLong *ptdts, PTAG ptagTdf)
{
    AssertThis(0);
    AssertNilOrPo(pstn, 0);
    AssertNilOrVarMem(ptdts);
    AssertNilOrVarMem(ptagTdf);

    if (pvNil != pstn)
        *pstn = _stn;
    if (pvNil != ptdts)
        *ptdts = _tdts;
    if (pvNil != ptagTdf)
        *ptagTdf = _tagTdf;
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
    AssertThis(0);
    AssertPo(pcfl, 0);
    AssertVarMem(pcno);

    ThreeDTextF tdtf;
    ChunkNumber cnoTdt;
    DataBlock blck;

    if (!_FWriteTmplf(pcfl, ctg, pcno))
        return fFalse;

    // Add ThreeDText chunk
    tdtf.bo = kboCur;
    tdtf.osk = koskCur;
    tdtf.tdts = _tdts;
    tdtf.tagTdf.From(_tagTdf);

    if (!pcfl->FAddChild(ctg, *pcno, kchidTdt, size(ThreeDTextF), kctgTdt, &cnoTdt, &blck))
    {
        return fFalse;
    }
    if (!blck.FWrite(&tdtf))
    {
        pcfl->DeleteChild(ctg, *pcno, kctgTdt, cnoTdt, kchidTdt);
        return fFalse;
    }
    return fTrue;
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
    AssertNilOrPo(_pgstAction, 0); // Localized action names are optional in headless hosts.
    Assert(_tagTdf.ctg != ctgNil, "ThreeDText has bad _tagTdf");
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
    AssertIn(_tdts, 0, tdtsLim);
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
    MarkMemObj(_pgstAction);
}

#endif // DEBUG
