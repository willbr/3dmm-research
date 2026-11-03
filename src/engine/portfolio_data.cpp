/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "portfolio_data.h"
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
        auto file = ChunkyFile::PcflOpen(selection, fcflNil);
        if (file)
        {
            ChunkIdentification movie;
            ChildChunkIdentification scene, thumbnail;
            DataBlock block;
            if (file->FGetCkiCtg('MVIE', 0, &movie) && file->FGetKidChidCtg('MVIE', movie.cno, 0, 'SCEN', &scene) &&
                file->FGetKidChidCtg('SCEN', scene.cki.cno, 0, 'THUM', &thumbnail) &&
                file->FFind(thumbnail.cki.ctg, thumbnail.cki.cno, &block))
                result = MaskedBitmapMBMP::PmbmpRead(&block);
            ReleasePpo(&file);
        }
    }
    if (!result && (previews & fpfPortPrevTexture))
        result = MaskedBitmapMBMP::PmbmpReadNative(selection, 0, 0, 0, fmbmpNil);
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
