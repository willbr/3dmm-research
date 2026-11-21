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
#include "handle_services.h"
ASSERTNAME

RTCLASS(ThreeDFont)

/***************************************************************************
    A PFNRPO to read a ThreeDFont from a file.
***************************************************************************/
MMFontResourceServices ThreeDFont::_Services()
{
    MMFontResourceServices s = {}; s.system = koskCur;
    const unsigned short one = 1; s.host_big_endian = !*reinterpret_cast<const unsigned char *>(&one);
    s.spacing = BR_SCALAR(0.0); s.leading = BR_SCALAR(0.5);
    s.object_bytes = sizeof(ThreeDFont);
    static_assert(sizeof(BRS) == 4 && sizeof(ChunkIdentification) == 8, "font ABI");
    s.state = [](void *, void *object) { return &static_cast<PThreeDFont>(object)->_font; };
    s.create = [](void *) -> void * { return NewObj ThreeDFont; };
    s.release = [](void *, void *object) { static_cast<BASE *>(object)->Release(); };
    s.decode = KauaiDecodeBlockHandle;
    s.allocator = KauaiCopyAllocator();
    s.invalidate = [](void *, void *out, size_t bytes) {
#ifdef DEBUG
        if (out) FillPb(out, static_cast<LegacyLong>(bytes), kbGarbage);
#else
        (void)out; (void)bytes;
#endif
    };
    s.bad_font = [](void *) { PushErc(ercSocBadTdf); };
    s.bad_order = [](void *) { Assert(false, "bad ThreeDFontF"); };
    s.child_count = [](void *, void *children) -> MMI32 { return static_cast<PDynamicArray>(children)->IvMac(); };
    s.child_at = [](void *, void *children, MMI32 i, MMChunkChild *out) {
        ChildChunkIdentification child; static_cast<PDynamicArray>(children)->Get(i, &child); *out = {child.cki.ctg, child.cki.cno, child.chid};
    };
    s.model_reader = ResourceReader(Model::FReadModl).Core();
    s.model_width = [](void *, void *model) -> MMI32 { return static_cast<PModel>(model)->Dxr(); };
    s.model_height = [](void *, void *model) -> MMI32 { return static_cast<PModel>(model)->Dyr(); };
    s.missing_model = [](void *, MMU32 id) {
        String message; message.FFormatSz(PszLit("Couldn't find BMDL for 3-D Font with chid %d."), id); Warn(message.Psz()); PushErc(ercSocNoModlForChar);
    };
    return s;
}

int ThreeDFont::FReadTdf(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block,
    void **out, MMI32 *bytes)
{
    const auto s = _Services();
    return mm_font_fetch(block, out, bytes, &s);
}

/***************************************************************************
    Initialize the font.  Does not clean up on failure because the
    destructor will.
***************************************************************************/
bool ThreeDFont::_FInit(PDataBlock pblck)
{
    AssertBaseThis(0); AssertPo(pblck, 0);
    const auto s = _Services();
    return mm_font_initialize(&_font, pblck->CoreBlock(), &s) != 0;
}

/***************************************************************************
    ThreeDFont destructor
***************************************************************************/
ThreeDFont::~ThreeDFont(void)
{
    AssertBaseThis(0);
    const auto s = _Services();
    mm_font_dispose(&_font, &s);
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
    const auto s = _Services();
    const auto file = pcrf->Pcfl();
    const auto edit = file ? file->EditIndex() : MMChunkEdit{};
    return mm_font_author(pcrf->CoreCache(), &edit, pglkid, pckiTdf, &s) != 0;
}

/***************************************************************************
    Get a model for a character from the font.  The chid is equal to the
    ASCII (or Unicode) value of the desired character.
***************************************************************************/
PModel ThreeDFont::PmodlFetch(ChildChunkID chid)
{
    AssertThis(0);
    const auto s = _Services();
    return static_cast<PModel>(mm_font_model(ResourceBinding(), chid, &s));
}

/***************************************************************************
    Return the width of the given character
***************************************************************************/
BRS ThreeDFont::DxrChar(LegacyLong ich)
{
    AssertThis(0); AssertIn(ich, 0, _font.count);
    return mm_font_width(&_font, ich);
}

/***************************************************************************
    Return the height of the given character
***************************************************************************/
BRS ThreeDFont::DyrChar(LegacyLong ich)
{
    AssertThis(0); AssertIn(ich, 0, _font.count);
    return mm_font_height(&_font, ich);
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
