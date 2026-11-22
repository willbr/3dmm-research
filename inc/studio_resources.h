/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_RESOURCES_H
#define STUDIO_RESOURCES_H
#include "kauai_core.h"
namespace StudioResources
{
// The product's startup tables contain either strings alone or one 32-bit
// extra value per string. Convert and validate those extras when requested.
// The caller owns each returned table.
PStringTable_GST ReadStringTable(MMResourceCache *, ChunkNumber, bool convertExtras = false);
PDynamicArray ReadPalette(MMResourceCache *, ChunkNumber);
} // namespace StudioResources
#endif
