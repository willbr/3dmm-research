/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdf.h: Studio bindings for Three-D Font resources

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#ifndef ENGINE_TDF_H
#define ENGINE_TDF_H
#include "mm/font_resource.h"

namespace EngineFont
{
int Read(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **object, MMI32 *bytes);
const MMFontOwnerHost &ReadHost();
bool Author(PChunkyResourceFile, PDynamicArray glyphs, ChunkIdentification * = nullptr);
PModel Glyph(const MMFontOwner *, ChildChunkID);
}
#endif
