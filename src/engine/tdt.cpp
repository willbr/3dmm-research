/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdt.cpp: Three-D Text class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    A 3D text template stores its descriptor in a TDT child of TMPL.
    C89 owns the descriptor, text, generated default material and last action.
    The native adapter supplies fonts, rendering and archive access.

***************************************************************************/
#include "soc_core.h"
ASSERTNAME

#ifdef DEBUG
#define TEXT_VALIDATE(owner) EngineTemplate::Validate(owner)
#else
#define TEXT_VALIDATE(owner) ((void)0)
#endif



static void TextTagToCore(const TAG *tag, MMTag *out)
{
    out->sid = tag->sid; std::memcpy(&out->cache, &tag->cache, sizeof(out->cache)); out->type = tag->ctg; out->number = tag->cno;
}
static void TextTagFromCore(TAG *tag, const MMTag *in)
{
    tag->sid = in->sid; std::memcpy(&tag->cache, &in->cache, sizeof(tag->cache)); tag->ctg = in->type; tag->cno = in->number;
}

void EngineText::SetFontResolver(PThreeDText owner, void *context, MMFontOwner *(*fetch)(void *, const MMTag *))
{
    *mm_template_owner_font(owner) = {context, fetch};
}

/***************************************************************************
    Return a list of all tags embedded in this ThreeDText.  Note that a
    return value of pvNil does not mean an error occurred, but simply that
    this ThreeDText has no embedded tags.

    Actually, as currently implemented, this function only returns pvNil
    if an error occurs.  The point is, look at *pfError, not the return
    value.
***************************************************************************/
PDynamicArray EngineText::PgltagFetch(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno, bool *pfError)
{
    AssertPo(pcfl, 0); AssertVarMem(pfError);
    MMTextResourceServices services = {};
    mm_kauai_text_read_services(&services);
    services.publish_error = [](void *, void *out, int error) { *static_cast<bool *>(out) = error != 0; };
    services.release_tag_list = [](void *, void *list) { static_cast<PDynamicArray>(list)->Release(); };
    services.tag_list = [](void *) -> void * { return DynamicArray::PglNew(sizeof(TAG)); };
    services.append_tag = [](void *, void *list, const MMTag *in) -> int {
        TAG tag;
        TextTagFromCore(&tag, in);
        return static_cast<PDynamicArray>(list)->FAdd(&tag);
    };
    const MMTextResourceSource source = {nullptr, pcfl, pcfl->ReadChunkAccess()};
    return static_cast<PDynamicArray>(mm_text_tags(&source, ctg, cno, pfError, &services));
}

/***************************************************************************
    Create a new ThreeDText
***************************************************************************/
PThreeDText EngineText::PtdtNew(PString pstn, LegacyLong tdts, PTAG ptagTdf, const MMTemplateFontProvider &provider)
{
    AssertPo(pstn, 0); AssertIn(tdts, 0, tdtsLim); AssertVarMem(ptagTdf);
    MMTag font;
    TextTagToCore(ptagTdf, &font);
    const MMStringValue name = {pstn->Pst(), sizeof(achar)};
    return mm_brender_text_create(&name, tdts, &font, &provider);
}

/***************************************************************************
    Read the generic Template info and the ThreeDText-specific info (tdts and tagTdf),
    then call _FInitLists to build the rest of the ThreeDText.
***************************************************************************/
bool EngineText::Initialize(PThreeDText owner, MMResourceCache *cache, ChunkTagOrType ctgTmpl, ChunkNumber cnoTmpl, MMOwnedBlock *block)
{
    Assert(cache, "missing template resource cache");
    return mm_brender_text_initialize(owner, cache, ctgTmpl, cnoTmpl, block) != 0;
}

/***************************************************************************
    Initialize or reinitialize the ThreeDText data structures, using the current
    _stn and _tdts.  This function unwinds completely on failure (the
    ThreeDText's members are untouched).
***************************************************************************/
MMTextGenerationServices EngineText::GenerationServices()
{
    MMTextGenerationServices s = {};
    static_assert(sizeof(BMAT34) == sizeof(MMActorMatrix) && sizeof(short) == 2, "text generation ABI");
    static_assert(krPi == 205887 && krTwoPi == 411774 && krThreePi == 617662 && krHalfPi == 102943 && BR_SCALAR(0.5 / PI) == 10430, "text constants");
    static_assert(tdtsLim == MM_TEXT_SHAPE_COUNT && tdaLim == MM_TEXT_ACTION_COUNT && tdtsCircleZ == 6 && tdaWalk == 13 && tdaStretch == 15, "text enums");
    mm_brender_text_generation_services(&s);
    return s;
}

/***************************************************************************
    Get the given action.  If we've built it before, use the cached copy.
    Else build the action, cache it, and return it.
***************************************************************************/
MMActionResource *EngineText::FetchAction(PThreeDText owner, LegacyLong tda)
{
    TEXT_VALIDATE(owner); AssertIn(tda, 0, tdaLim);
    auto services = GenerationServices();
    return mm_text_fetch_action(owner, tda, &services);
}

/***************************************************************************
    Get the name of the given action
***************************************************************************/
bool EngineText::ActionName(PThreeDText owner, LegacyLong anid, PString pstn)
{
    TEXT_VALIDATE(owner); AssertIn(anid, 0, tdaLim); AssertPo(pstn, 0);
    const MMStringValue name = {pstn->Pst(), sizeof(achar)};
    return mm_brender_template_action_name(owner, anid, &name) != 0;
}

/***************************************************************************
    Fetch the given model for this ThreeDText (use the ThreeDText's current font)
***************************************************************************/
PModel EngineText::FetchModel(PThreeDText owner, ChildChunkID chidModl)
{
    TEXT_VALIDATE(owner); AssertIn(chidModl, 0, GenerationServices().length(nullptr, owner));
    auto services = GenerationServices();
    return static_cast<PModel>(mm_text_fetch_model(owner, chidModl, &services));
}

/***************************************************************************
    Return a duplicate of this ThreeDText
***************************************************************************/
PThreeDText EngineText::PtdtDup(PThreeDText owner)
{
    return mm_brender_text_duplicate(owner);
}

/***************************************************************************
    Change the text, shape, and/or font of the ThreeDText
***************************************************************************/
bool EngineText::FChange(PThreeDText owner, PString pstn, LegacyLong tdts, PTAG ptagTdf)
{
    AssertNilOrPo(pstn, 0); AssertNilOrVarMem(ptagTdf);
    if (tdts != tdtsNil) AssertIn(tdts, 0, tdtsLim);
    MMTag font;
    if (ptagTdf) TextTagToCore(ptagTdf, &font);
    const MMStringValue name = {pstn ? pstn->Pst() : nullptr, sizeof(achar)};
    return mm_brender_text_change(owner, pstn ? &name : nullptr, tdts, ptagTdf ? &font : nullptr) != 0;
}

/***************************************************************************
    Get stats of this ThreeDText
***************************************************************************/
void EngineText::GetInfo(PThreeDText owner, PString pstn, LegacyLong *ptdts, PTAG ptagTdf)
{
    TEXT_VALIDATE(owner); AssertNilOrPo(pstn, 0); AssertNilOrVarMem(ptdts); AssertNilOrVarMem(ptagTdf);
    const auto *descriptor = mm_template_owner_descriptor(owner);
    if (pstn)
    {
        const MMStringValue name = {pstn->Pst(), sizeof(achar)};
        mm_string_value_copy(&name, mm_template_owner_name(owner).buffer);
    }
    if (ptdts) *ptdts = descriptor->shape;
    if (ptagTdf) TextTagFromCore(ptagTdf, &descriptor->font);
}

/***************************************************************************
    Adjust the given body's shape, since its owning ThreeDText has changed
***************************************************************************/
bool EngineText::FAdjustBody(PThreeDText owner, PBody pbody)
{
    TEXT_VALIDATE(owner); Assert(pbody, "missing body");
    return mm_brender_text_adjust_body(owner, pbody) != 0;
}

/***************************************************************************
    Set the default costume.  This always succeeds for ThreeDTexts because they
    keep the PMaterial_MTRL in memory.
***************************************************************************/
bool EngineText::FSetDefaultCost(PThreeDText owner, PBody pbody)
{
    TEXT_VALIDATE(owner); Assert(pbody, "missing body");
    return mm_brender_template_default_costume(owner, pbody) != 0;
}

/***************************************************************************
    Write the ThreeDText out as a Template hierarchy.
***************************************************************************/
bool EngineText::FWrite(PThreeDText owner, PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    AssertPo(pcfl, 0);
    return FWrite(owner, pcfl->WriteOwnerIndex(), ctg, pcno);
}

bool EngineText::FWrite(PThreeDText owner, const MMArchiveWriter &writer, ChunkTagOrType ctg, ChunkNumber *pcno)
{
    TEXT_VALIDATE(owner); AssertVarMem(pcno);
    return mm_text_write_owner(owner, ctg, pcno, &writer) != 0;
}

bool EngineText::FWriteDraft(PChunkyFile file, PString name, LegacyLong shape, const TAG &font, ChunkNumber *number)
{
    AssertPo(file, 0);
    AssertPo(name, 0);
    AssertVarMem(number);
    MMTextDescriptor draft = {};
    draft.shape = shape;
    TextTagToCore(&font, &draft.font);
    const auto writer = file->WriteIndex();
    const MMStringValue value = {name->Pst(), sizeof(achar)};
    return mm_text_write_draft(&value, &draft, number, &writer) != 0;
}
