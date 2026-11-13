/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdf.cpp: Three-D Font class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!


    ThreeDFonts (3-D Fonts) are simply collections of models, one model per ASCII
    character.	The ThreeDFont class holds general font information such as the
    count of characters in the font and the maximum height of the
    characters.  It also holds an array of widths and heights of every
    character, to allow proportional spacing.  Fetching a letter's model
    from a ThreeDFont involves	looking for a child chunk of the ThreeDFont chunk with a
    ChildChunkID equal to the ASCII value of the desired character:

    ThreeDFont  // Contains font info (width and height of characters)
     |
     +---BMDL (chid 0) // Model for ASCII character 0
     |
     +---BMDL (chid 1) // Model for ASCII character 1
     .
     .
     .

***************************************************************************/
#include "soc_core.h"
ASSERTNAME

RTCLASS(ThreeDFont)

/***************************************************************************
    A PFNRPO to read a ThreeDFont from a file.
***************************************************************************/
struct FontResourceContext { DataBlock block; };
ThreeMMFontResourceServices ThreeDFont::_Services(FontResourceContext *context)
{
    ThreeMMFontResourceServices s = {}; s.context = context; s.system = koskCur;
    const unsigned short one = 1; s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
    s.spacing = BR_SCALAR(0.0); s.leading = BR_SCALAR(0.5);
    static_assert(sizeof(BRS) == 4 && sizeof(ChunkIdentification) == 8, "font ABI");
    s.state = [](void *, void *object) { return &static_cast<PThreeDFont>(object)->_font; };
    s.create = [](void *) -> void * { return NewObj ThreeDFont; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.initialize = [](void *, void *object, void *block) -> int { return static_cast<PThreeDFont>(object)->_FInit(static_cast<PDataBlock>(block)); };
    s.object_size = [](void *) -> ThreeMMI32 { return sizeof(ThreeDFont); };
    s.packed = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FPacked(); };
    s.unpack = [](void *, void *block) -> int { return static_cast<PDataBlock>(block)->FUnpackData(); };
    s.block_size = [](void *, void *block) -> ThreeMMI32 { return static_cast<PDataBlock>(block)->Cb(); };
    s.read = [](void *, void *block, void *out, ThreeMMI32 bytes, ThreeMMI32 offset) -> int { return static_cast<PDataBlock>(block)->FReadRgb(out, bytes, offset); };
    s.write = [](void *, void *block, const void *data, ThreeMMI32 bytes, ThreeMMI32 offset) -> int { return static_cast<PDataBlock>(block)->FWriteRgb(const_cast<void *>(data), bytes, offset); };
    s.allocate = [](void *, ThreeMMI32 bytes, int clear, void **out) -> int { return FAllocPv(out, bytes, clear ? fmemClear : fmemNil, mprNormal); };
    s.free_memory = [](void *, void **out) { FreePpv(out); };
    s.invalidate = [](void *, void *out, size_t bytes) {
#ifdef DEBUG
        if (out) FillPb(out, static_cast<LegacyLong>(bytes), kbGarbage);
#else
        (void)out; (void)bytes;
#endif
    };
    s.bad_font = [](void *) { PushErc(ercSocBadTdf); };
    s.bad_order = [](void *) { Assert(false, "bad ThreeDFontF"); };
    s.child_count = [](void *, void *children) -> ThreeMMI32 { return static_cast<PDynamicArray>(children)->IvMac(); };
    s.child_at = [](void *, void *children, ThreeMMI32 i, ThreeMMFontChild *out) {
        ChildChunkIdentification child; static_cast<PDynamicArray>(children)->Get(i, &child); *out = {child.cki.ctg, child.cki.cno, child.chid};
    };
    s.file = [](void *, void *cache) -> void * { return static_cast<PChunkyResourceFile>(cache)->Pcfl(); };
    s.add = [](void *p, void *file, ThreeMMI32 bytes, ThreeMMU32 type, ThreeMMU32 *out) -> void * {
        ChunkNumber number = *out; auto block = &static_cast<FontResourceContext *>(p)->block;
        const bool ok = static_cast<PChunkyFile>(file)->FAdd(bytes, type, &number, block); *out = number; return ok ? block : nullptr;
    };
    s.fetch_model = [](void *, void *cache, ThreeMMU32 type, ThreeMMU32 number) -> void * { return static_cast<PChunkyResourceFile>(cache)->PbacoFetch(type, number, Model::FReadModl); };
    s.adopt = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, const ThreeMMFontChild *child) -> int {
        return static_cast<PChunkyFile>(file)->FAdoptChild(type, number, child->type, child->number, child->id);
    };
    s.model_width = [](void *, void *model) -> ThreeMMI32 { return static_cast<PModel>(model)->Dxr(); };
    s.model_height = [](void *, void *model) -> ThreeMMI32 { return static_cast<PModel>(model)->Dyr(); };
    s.binding = [](void *, void *object, void **cache, ThreeMMFontChunk *chunk) {
        auto font = static_cast<PThreeDFont>(object); *cache = font->Pcrf(); *chunk = {font->Ctg(), font->Cno()};
    };
    s.child_id = [](void *, void *file, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 id, ThreeMMFontChild *out) -> int {
        ChildChunkIdentification child;
        if (!static_cast<PChunkyFile>(file)->FGetKidChid(type, number, id, &child)) return false;
        *out = {child.cki.ctg, child.cki.cno, child.chid}; return true;
    };
    s.missing_model = [](void *, ThreeMMU32 id) {
        String message; message.FFormatSz(PszLit("Couldn't find BMDL for 3-D Font with chid %d."), id); Warn(message.Psz()); PushErc(ercSocNoModlForChar);
    };
    return s;
}

bool ThreeDFont::FReadTdf(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb)
{
    AssertPo(pcrf, 0); AssertPo(pblck, 0); AssertNilOrVarMem(ppbaco); AssertVarMem(pcb);
    (void)ctg; (void)cno;
    FontResourceContext context; auto s = _Services(&context);
    return three_mm_font_fetch(pblck, ppbaco, pcb, &s) != 0;
}

/***************************************************************************
    Initialize the font.  Does not clean up on failure because the
    destructor will.
***************************************************************************/
bool ThreeDFont::_FInit(PDataBlock pblck)
{
    AssertBaseThis(0); AssertPo(pblck, 0);
    FontResourceContext context; auto s = _Services(&context);
    return three_mm_font_initialize(&_font, pblck, &s) != 0;
}

/***************************************************************************
    ThreeDFont destructor
***************************************************************************/
ThreeDFont::~ThreeDFont(void)
{
    AssertBaseThis(0);
    FontResourceContext context; auto s = _Services(&context);
    three_mm_font_dispose(&_font, &s);
}

/***************************************************************************
    This authoring-only API creates a new ThreeDFont chunk in pcrf, with child
    models as specified in pglkid.  This function does not create a new
    ThreeDFont instance in memory...to do that, call FReadTdf with the values
    returned in pckiTdf.
***************************************************************************/
bool ThreeDFont::FCreate(PChunkyResourceFile pcrf, PDynamicArray pglkid, String *pstn, ChunkIdentification *pckiTdf)
{
    AssertPo(pcrf, 0); AssertPo(pglkid, 0); AssertPo(pstn, 0); AssertNilOrVarMem(pckiTdf);
    FontResourceContext context; auto s = _Services(&context);
    return three_mm_font_author(pcrf, pglkid, pckiTdf, &s) != 0;
}

/***************************************************************************
    Get a model for a character from the font.  The chid is equal to the
    ASCII (or Unicode) value of the desired character.
***************************************************************************/
PModel ThreeDFont::PmodlFetch(ChildChunkID chid)
{
    AssertThis(0);
    FontResourceContext context; auto s = _Services(&context);
    return static_cast<PModel>(three_mm_font_model(this, chid, &s));
}

/***************************************************************************
    Return the width of the given character
***************************************************************************/
BRS ThreeDFont::DxrChar(LegacyLong ich)
{
    AssertThis(0); AssertIn(ich, 0, _font.count);
    return three_mm_font_width(&_font, ich);
}

/***************************************************************************
    Return the height of the given character
***************************************************************************/
BRS ThreeDFont::DyrChar(LegacyLong ich)
{
    AssertThis(0); AssertIn(ich, 0, _font.count);
    return three_mm_font_height(&_font, ich);
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the ThreeDFont.
***************************************************************************/
void ThreeDFont::AssertValid(LegacyUlong grf)
{
    ThreeDFont_PAR::AssertValid(fobjAllocated);
    AssertIn(_font.count, 0, klwMax);
    AssertIn(_font.maximum_height, 0, BR_SCALAR_MAX);
    AssertPvCb(_font.widths, LwMul(_font.count, size(BRS)));
    AssertPvCb(_font.heights, LwMul(_font.count, size(BRS)));
}

/***************************************************************************
    Mark memory used by the ThreeDFont
***************************************************************************/
void ThreeDFont::MarkMem(void)
{
    AssertThis(0);
    ThreeDFont_PAR::MarkMem();
    MarkPv(_font.widths);
    MarkPv(_font.heights);
}
#endif // DEBUG
