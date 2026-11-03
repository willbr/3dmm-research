/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PORTFOLIO_HOST_H
#define STUDIO_PORTFOLIO_HOST_H
#include "portf.h"
namespace StudioPortfolio
{
// Synchronous request. Borrowed strings retain Studio's legacy encoding;
// filter is the original double-NUL-terminated label/pattern list. A backend
// must preserve preview flags, audio cues and Studio's overwrite interaction.
// No window handles, native callbacks or common-dialog flags cross this API.
struct Request
{
    bool open = true;
    achar *filter = nullptr;
    achar *title = nullptr;
    achar *defaultExtension = nullptr;    // Save only.
    PString defaultName = nullptr;        // Save only.
    Filename *initialDirectory = nullptr; // Open only.
    LegacyUlong previews = fpfNil;
    ChunkNumber sound = 0;
};
// Does not open or write the chosen movie. Returns false on cancellation or
// error. Open cancellation leaves selection untouched; save cancellation clears
// it, matching the existing portfolio. The caller performs the movie command.
bool Choose(Filename *selection, const Request &request);
} // namespace StudioPortfolio
#endif
