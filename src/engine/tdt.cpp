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



struct TextResourceContext
{
    String saved;
};
static void TextTagToCore(const TAG *tag, MMTag *out)
{
    out->sid = tag->sid; std::memcpy(&out->cache, &tag->cache, sizeof(out->cache)); out->type = tag->ctg; out->number = tag->cno;
}
static void TextTagFromCore(TAG *tag, const MMTag *in)
{
    tag->sid = in->sid; std::memcpy(&tag->cache, &in->cache, sizeof(tag->cache)); tag->ctg = in->type; tag->cno = in->number;
}
static MMTextResourceServices ResourceServices(void *context)
{
    MMTextResourceServices s = {}; s.context = context; s.generation = EngineText::GenerationServices();
    mm_kauai_text_read_services(&s);
    static_assert(sizeof(LegacyLong) == 4 && sizeof(ChunkNumber) == 4 && sizeof(TAG) == sizeof(MMTag), "text resource ABI");
    s.state = [](void *, void *object) { return mm_template_owner_descriptor(static_cast<PThreeDText>(object)); };
    s.create = [](void *) -> void * { return mm_template_owner_create(&EngineTemplate::Host(), 1); };
    s.retain_object = [](void *, void *object) { static_cast<BASE *>(object)->AddRef(); };
    s.release_object = [](void *, void *object) { mm_template_owner_release(static_cast<PThreeDText>(object)); };
    s.text = [](void *, void *object) -> void * {
        return mm_template_owner_name(static_cast<PThreeDText>(object)).buffer;
    };
    s.assign = [](void *, void *destination, const void *source) {
        const MMStringValue name = {destination, sizeof(achar)};
        mm_string_value_copy(&name, source);
    };
    s.space = [](void *, void *text) {
        const MMStringValue name = {text, sizeof(achar)};
        const achar ch = ChLit(' ');
        mm_string_value_set(&name, &ch, 1);
    };
    s.saved_text = [](void *p) -> void * { return static_cast<TextResourceContext *>(p)->saved.Pst(); };
    s.provider_copy = [](void *, void *to, void *from) {
        *mm_template_owner_font(static_cast<PThreeDText>(to)) = *mm_template_owner_font(static_cast<PThreeDText>(from));
    };
    s.publish_tag = [](void *, void *out, const MMTag *in) { TextTagFromCore(static_cast<PTAG>(out), in); };
    s.publish_error = [](void *, void *out, int error) { *static_cast<bool *>(out) = error != 0; };
    s.release_tag_list = [](void *, void *list) { static_cast<PDynamicArray>(list)->Release(); };
    s.tag_list = [](void *) -> void * { return DynamicArray::PglNew(sizeof(TAG)); };
    s.append_tag = [](void *, void *list, const MMTag *in) -> int { TAG tag; TextTagFromCore(&tag, in); return static_cast<PDynamicArray>(list)->FAdd(&tag); };
    return s;
}

MMTextNames &EngineText::ActionNames()
{
    return *mm_brender_text_action_names();
}

void EngineText::SetFontResolver(PThreeDText owner, void *context, MMFontOwner *(*fetch)(void *, const MMTag *))
{
    *mm_template_owner_font(owner) = {context, fetch};
}

/***************************************************************************
    Set the StringTable_GST of action names for ThreeDTexts
***************************************************************************/
bool EngineText::FSetActionNames(PStringTable_GST pgstAction)
{
    AssertPo(pgstAction, 0); Assert(ActionNames().table == pvNil, "you already set the action names");
    TextResourceContext context; auto services = ResourceServices(&context);
    return mm_text_set_names(&ActionNames(), pgstAction, &services) != 0;
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
    TextResourceContext context; auto services = ResourceServices(&context);
    const MMTextResourceSource source = {nullptr, pcfl, pcfl->ReadChunkAccess()};
    return static_cast<PDynamicArray>(mm_text_tags(&source, ctg, cno, pfError, &services));
}

/***************************************************************************
    Create a new ThreeDText
***************************************************************************/
PThreeDText EngineText::PtdtNew(PString pstn, LegacyLong tdts, PTAG ptagTdf, const MMTemplateFontProvider &provider)
{
    AssertPo(pstn, 0); AssertIn(tdts, 0, tdtsLim); AssertVarMem(ptagTdf);
    TextResourceContext context; auto services = ResourceServices(&context); MMTag font;
    TextTagToCore(ptagTdf, &font);
    auto owner = static_cast<PThreeDText>(mm_text_create(pstn->Pst(), tdts, &font, &services));
    if (owner)
        *mm_template_owner_font(owner) = provider;
    return owner;
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
    TEXT_VALIDATE(owner);
    TextResourceContext context; auto services = ResourceServices(&context);
    return static_cast<PThreeDText>(mm_text_duplicate(owner, &services));
}

/***************************************************************************
    Change the text, shape, and/or font of the ThreeDText
***************************************************************************/
bool EngineText::FChange(PThreeDText owner, PString pstn, LegacyLong tdts, PTAG ptagTdf)
{
    TEXT_VALIDATE(owner); AssertNilOrPo(pstn, 0); AssertNilOrVarMem(ptagTdf);
    if (tdts != tdtsNil) AssertIn(tdts, 0, tdtsLim);
    TextResourceContext context; auto services = ResourceServices(&context); MMTag font;
    if (ptagTdf) TextTagToCore(ptagTdf, &font);
    return mm_text_change(owner, pstn ? pstn->Pst() : nullptr, tdts, ptagTdf ? &font : nullptr, &services) != 0;
}

/***************************************************************************
    Get stats of this ThreeDText
***************************************************************************/
void EngineText::GetInfo(PThreeDText owner, PString pstn, LegacyLong *ptdts, PTAG ptagTdf)
{
    TEXT_VALIDATE(owner); AssertNilOrPo(pstn, 0); AssertNilOrVarMem(ptdts); AssertNilOrVarMem(ptagTdf);
    TextResourceContext context; auto services = ResourceServices(&context);
    mm_text_info(owner, pstn ? pstn->Pst() : nullptr, ptdts, ptagTdf, &services);
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
    byte wire[kcbMaxDataStn];
    String name;
    EngineTemplate::GetName(owner, &name);
    const auto encoded = ChunkyFile::EncodeName(&name, wire);
    return mm_text_write(mm_template_owner_state(owner), mm_template_owner_descriptor(owner), &encoded, ctg, pcno, &writer) != 0;
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
    byte wire[kcbMaxDataStn];
    const auto encoded = ChunkyFile::EncodeName(name, wire);
    return mm_text_write_draft(&encoded, &draft, number, &writer) != 0;
}

#ifdef DEBUG
/***************************************************************************
    Mark memory used by the ThreeDText action StringTable_GST
***************************************************************************/
void EngineText::MarkActionNames(void)
{
    MarkMemObj(static_cast<PStringTable_GST>(ActionNames().table));
}

#endif // DEBUG
