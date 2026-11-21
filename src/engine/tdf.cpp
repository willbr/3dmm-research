/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdf.cpp: Studio bindings for Three-D Font resources

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!


    ThreeDFonts (3-D Fonts) are simply collections of models, one model per ASCII
    character. A ThreeDFont holds general font information such as the
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

namespace
{
void Invalidate(void *, void *out, size_t bytes)
{
#ifdef DEBUG
    if (out)
        FillPb(out, static_cast<LegacyLong>(bytes), kbGarbage);
#else
    (void)out;
    (void)bytes;
#endif
}

const MMFontResourceServices &GlyphServices()
{
    static const auto services = [] {
        MMFontResourceServices s = {};
        const unsigned short one = 1;
        s.host_big_endian = *reinterpret_cast<const unsigned char *>(&one) == 0;
        s.system = koskCur;
        s.leading = BR_SCALAR(0.5);
        s.allocator = KauaiCopyAllocator();
        s.invalidate = Invalidate;
        s.release = [](void *, void *object) { mm_model_owner_release(static_cast<PModel>(object)); };
        s.child_count = [](void *, void *children) -> MMI32 { return static_cast<PDynamicArray>(children)->IvMac(); };
        s.child_at = [](void *, void *children, MMI32 i, MMChunkChild *out) {
            ChildChunkIdentification child;
            static_cast<PDynamicArray>(children)->Get(i, &child);
            *out = {child.cki.ctg, child.cki.cno, child.chid};
        };
        s.model_reader = EngineModel::Read;
        s.model_objects = mm_model_owner_services();
        s.model_width = [](void *, void *model) -> MMI32 { return EngineModel::Extent(static_cast<PModel>(model), 0); };
        s.model_height = [](void *, void *model) -> MMI32 { return EngineModel::Extent(static_cast<PModel>(model), 1); };
        s.missing_model = [](void *, MMU32 id) {
            String message;
            message.FFormatSz(PszLit("Couldn't find BMDL for 3-D Font with chid %d."), id);
            Warn(message.Psz());
            PushErc(ercSocNoModlForChar);
        };
        return s;
    }();
    return services;
}
}

const MMFontOwnerHost &EngineFont::ReadHost()
{
    static const MMFontOwnerHost host = {
        KauaiResourceObjectAllocator(), KauaiCopyAllocator(), KauaiDecodeBlockHandle,
#ifdef DEBUG
        [](void *memory) { MarkPv(memory); },
#else
        nullptr,
#endif
        [](void *) { PushErc(ercSocBadTdf); },
        [](void *) { Assert(false, "bad ThreeDFontF"); },
        Invalidate
    };
    return host;
}

int EngineFont::Read(MMResourceCache *, MMU32, MMU32, MMOwnedBlock *block, void **out, MMI32 *bytes)
{
    return mm_font_owner_read(block, out, bytes, &ReadHost());
}

bool EngineFont::Author(PChunkyResourceFile cache, PDynamicArray glyphs, ChunkIdentification *out)
{
    AssertPo(cache, 0); AssertPo(glyphs, 0); AssertNilOrVarMem(out);
    static_assert(sizeof(BRS) == 4 && sizeof(ChunkIdentification) == 8, "font ABI");
    const auto writer = cache->ArchiveWriter();
    return mm_font_author(cache->CoreCache(), &writer.edit, glyphs, out, &GlyphServices()) != 0;
}

PModel EngineFont::Glyph(const MMFontOwner *font, ChildChunkID slot)
{
    return static_cast<PModel>(mm_font_owner_model(font, slot, &GlyphServices()));
}
