/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "portfolio_data.h"
#include "mm/portfolio.h"
#include "resource_archive_host.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Portfolio data must not import the Windows SDK
#endif

namespace StudioPortfolio
{
namespace
{
struct PreviewErrors
{
    ErrorStack errors;
    ErrorStack *previous = vpers;
    PreviewErrors()
    {
        vpers = &errors;
    }
    ~PreviewErrors()
    {
        vpers = previous;
    }
};
} // namespace

PMaskedBitmapMBMP LoadPreview(Filename *selection, LegacyUlong previews)
{
    static_assert(int(fpfPortPrevMovie) == MM_PORTFOLIO_PREVIEW_MOVIE &&
                  int(fpfPortPrevTexture) == MM_PORTFOLIO_PREVIEW_BITMAP, "portfolio preview flags");
    if (!selection || !previews || selection->FDir())
        return nullptr;
    PreviewErrors errors;
    const MMPortfolioPreviewServices services = {
        [](void *file) { return OpenWorkingResourceCache(static_cast<PFilename>(file), false); },
        ReadCachedMaskedBitmap,
        [](void *file) { return ImportMaskedBitmap(static_cast<PFilename>(file)); }
    };
    return mm_portfolio_preview(selection, previews, &services);
}

PMaskedBitmapMBMP LoadPreviewPath(PString path, LegacyUlong previews)
{
    if (!path || !previews || !path->Cch())
        return nullptr;
    PreviewErrors errors;
    Filename selection;
    return selection.FBuildFromPath(path, 0) ? LoadPreview(&selection, previews) : nullptr;
}

bool AppendSaveExtension(PString path, PZString extension, bool different)
{
    if (!different)
        return true;
    if (!path || !extension)
        return false;
    const MMStringValue name = {path->Pst(), sizeof(achar)};
    return mm_portfolio_append_extension(&name, extension, CchSz(extension)) != 0;
}

void RemoveSaveExtension(PString leaf)
{
    const MMStringValue name = {leaf->Pst(), sizeof(achar)};
    mm_portfolio_remove_extension(&name);
}
} // namespace StudioPortfolio
