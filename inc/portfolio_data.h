/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PORTFOLIO_DATA_H
#define STUDIO_PORTFOLIO_DATA_H
#include "portf.h"
#include "mbmp.h"
namespace StudioPortfolio
{
// UI-thread operation: preview failures are isolated from the application's
// error stack. Returns an owned bitmap (ReleasePpo), or null for no preview.
// Preserves movie-first / bitmap-second priority; sound has no visual preview.
// Indexed pixels use the caller's Studio palette, including BMP previews.
PMaskedBitmapMBMP LoadPreview(Filename *selection, LegacyUlong previews);
PMaskedBitmapMBMP LoadPreviewPath(PString path, LegacyUlong previews);
// The picker identifies whether the entered extension differs from its default.
// Append the original default suffix in that case, retaining any entered suffix.
// Transactional on overflow; neither routine touches the filesystem.
bool AppendSaveExtension(PString path, PZString extension, bool different);
void RemoveSaveExtension(PString leaf);
} // namespace StudioPortfolio
#endif
