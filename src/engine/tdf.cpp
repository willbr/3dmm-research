/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdf.cpp: Native collection adapter for Three-D Font authoring

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
ASSERTNAME

bool EngineFont::Author(PChunkyResourceFile cache, PDynamicArray glyphs, ChunkIdentification *out)
{
    AssertPo(cache, 0); AssertPo(glyphs, 0); AssertNilOrVarMem(out);
    static_assert(sizeof(BRS) == 4 && sizeof(ChunkIdentification) == 8, "font ABI");
    MMFontResourceServices services = {};
    mm_brender_asset_font_bind(&services);
    services.system = koskCur;
    services.leading = BR_SCALAR(0.5);
    services.allocator = mm_kauai_font_host()->metrics;
    services.invalidate = mm_kauai_font_host()->invalidate;
    services.child_count = [](void *, void *children) -> MMI32 { return static_cast<PDynamicArray>(children)->IvMac(); };
    services.child_at = [](void *, void *children, MMI32 i, MMChunkChild *out) {
        ChildChunkIdentification child;
        static_cast<PDynamicArray>(children)->Get(i, &child);
        *out = {child.cki.ctg, child.cki.cno, child.chid};
    };
    const auto writer = cache->ArchiveWriter();
    return mm_font_author(cache->CoreCache(), &writer.edit, glyphs, out, &services) != 0;
}
