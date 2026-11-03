/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "frame.h"
ASSERTNAME
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
static void Check(bool condition)
{
    if (!condition)
        std::exit(1);
}
int main()
{
    RC rectangle(0, 0, 10, 20);
    PRegion region = Region::PregnNew(&rectangle);
    RECT bounds;
    Check(region && GetRgnBox(region->HrgnEnsure(), &bounds) == SIMPLEREGION && bounds.right == 10);
    region->Offset(70000, -70000);
    Check(GetRgnBox(region->HrgnEnsure(), &bounds) == SIMPLEREGION && bounds.left == 70000 && bounds.top == -70000);
    region->Scale(1, 1, 1, 1); // Rectangular early-return path must discard stale native state.
    Check(GetRgnBox(region->HrgnEnsure(), &bounds) == SIMPLEREGION && bounds.left == 70000);
    region->Scale(0, 1, 0, 1);
    Check(GetRgnBox(region->HrgnEnsure(), &bounds) == NULLREGION);
    ReleasePpo(&region);

    HDC recording = CreateEnhMetaFileA(nullptr, nullptr, nullptr, nullptr);
    Check(recording != nullptr);
    Check(Rectangle(recording, 0, 0, 10, 20));
    HENHMETAFILE native = CloseEnhMetaFile(recording);
    PPicture picture = Picture::PpicNew(native, &rectangle);
    Check(picture != pvNil);
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    ChunkNumber first, second;
    DataBlock block, copy;
    Check(file && file->FAdd(picture->CbOnFile(), kctgMeta, &first, &block) && picture->FWrite(&block));
    PPicture loaded = Picture::PpicRead(&block);
    Check(loaded && loaded->CbOnFile() == block.Cb());
    Check(file->FAdd(loaded->CbOnFile(), kctgMeta, &second, &copy) && loaded->FWrite(&copy));
    std::vector<byte> before(block.Cb()), after(copy.Cb());
    Check(block.FReadRgb(before.data(), block.Cb(), 0) && copy.FReadRgb(after.data(), copy.Cb(), 0));
    Check(before == after); // Loading through GDI must not canonicalize stored bytes.
    ReleasePpo(&picture);
    ReleasePpo(&loaded);
    ReleasePpo(&file);
    return 0;
}
