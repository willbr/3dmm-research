/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "portfolio_data.h"
#include "mm/scene_thumbnail.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Portfolio data must not import the Windows SDK
#endif
ASSERTNAME
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
    if (!selection || !previews || selection->FDir())
        return nullptr;
    PreviewErrors errors;
    PMaskedBitmapMBMP result = nullptr;
    if (previews & fpfPortPrevMovie)
    {
        auto cache = ChunkyResourceFile::PcrfOpenWorking(selection, false);
        if (cache)
        {
            const auto chunks = cache->ReadChunkAccess();
            MMU32 thumbnail;
            if (mm_movie_first_scene_thumbnail(&chunks, &thumbnail) == MM_CHUNK_OK)
                result = FetchMaskedBitmap(cache, MM_SCENE_THUMBNAIL_TYPE, thumbnail);
            ReleasePpo(&cache);
        }
    }
    if (!result && (previews & fpfPortPrevTexture))
        result = ImportMaskedBitmap(selection, 0, 0, 0, fmbmpNil);
    return result;
}
bool AppendSaveExtension(PString path, PZString extension, bool different)
{
    if (!different)
        return true;
    if (!path || !extension)
        return false;
    String changed = *path;
    if (!changed.FAppendCh('.') || !changed.FAppendSz(extension))
        return false;
    *path = changed;
    return true;
}
PMaskedBitmapMBMP LoadPreviewPath(PString path, LegacyUlong previews)
{
    if (!path || !previews || !path->Cch())
        return nullptr;
    PreviewErrors errors;
    Filename selection;
    return selection.FBuildFromPath(path, 0) ? LoadPreview(&selection, previews) : nullptr;
}
void RemoveSaveExtension(PString leaf)
{
    for (auto position = leaf->Cch(); position-- > 0;)
        if (leaf->Prgch()[position] == ChLit('.'))
        {
            leaf->Delete(position);
            break;
        }
}
} // namespace StudioPortfolio
