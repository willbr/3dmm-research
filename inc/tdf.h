/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdf.h: Native collection adapter for Three-D Font authoring

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

***************************************************************************/
#ifndef ENGINE_TDF_H
#define ENGINE_TDF_H
#include "mm/font_resource.h"

namespace EngineFont
{
bool Author(PChunkyResourceFile, PDynamicArray glyphs, ChunkIdentification * = nullptr);
}
#endif
