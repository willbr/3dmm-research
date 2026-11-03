/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_RESOURCES_H
#define STUDIO_RESOURCES_H
#include "kauai_core.h"
namespace StudioResources
{
// Studio resource tables contain either strings alone or one 32-bit extra
// value per string. The returned table is owned by the caller, with extras
// converted to host byte order. Generic GST readers leave extras untouched.
PStringTable_GST ReadStringTable(PDataBlock block);
} // namespace StudioResources
#endif
