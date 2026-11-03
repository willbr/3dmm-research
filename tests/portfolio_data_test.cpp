/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "portfolio_data.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Portfolio data tests must run without the Windows SDK
#endif
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
    std::exit(1);
}
static void Check(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "Portfolio data check failed at %d\n", line);
        std::exit(1);
    }
}
#define CHECK(value) Check(value, __LINE__)
static void CheckPixels(PMaskedBitmapMBMP bitmap, const byte *expected)
{
    CHECK(bitmap != nullptr);
    RC bounds;
    bitmap->GetRc(&bounds);
    CHECK(bounds.Dxp() == 3 && bounds.Dyp() == 2);
    byte decoded[8] = {};
    bitmap->Draw(decoded, 4, 2, -bounds.xpLeft, -bounds.ypTop);
    CHECK(std::memcmp(decoded, expected, 8) == 0);
    ReleasePpo(&bitmap);
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    String base;
    base.SetSzs(argv[1]);
    String path = base;
    CHECK(path.FAppendSz(PszLit(".3mm")));
    Filename movie;
    CHECK(movie.FBuildFromPath(&path));
    auto file = ChunkyFile::PcflCreate(&movie, fcflWriteEnable);
    CHECK(file);
    ChunkNumber movieNumber, sceneNumber, thumbNumber;
    DataBlock block;
    CHECK(file->FAdd(0, 'MVIE', &movieNumber));
    CHECK(file->FAddChild('MVIE', movieNumber, 0, 0, 'SCEN', &sceneNumber));
    byte pixels[8] = {1, 2, 3, 0, 4, 0, 6, 0};
    RC bounds(0, 0, 3, 2);
    auto bitmap = MaskedBitmapMBMP::PmbmpNew(pixels, 4, 2, &bounds, 1, 1, 0);
    CHECK(bitmap);
    CHECK(file->FAddChild('SCEN', sceneNumber, 0, bitmap->CbOnFile(), 'THUM', &thumbNumber, &block));
    CHECK(bitmap->FWrite(&block) && file->FSave('SOC '));
    block.Free();
    ReleasePpo(&bitmap);
    ReleasePpo(&file);
    PushErc(ercFniDelete); // An unrelated queued error must survive probing.
    const auto previous = vpers;
    const auto errorCount = vpers->Cerc();
    CheckPixels(StudioPortfolio::LoadPreview(&movie, fpfPortPrevMovie), pixels);
    CheckPixels(StudioPortfolio::LoadPreviewPath(&path, fpfPortPrevMovie | fpfPortPrevTexture), pixels);
    CHECK(!StudioPortfolio::LoadPreview(&movie, fpfPortPrevSound));
    CHECK(!StudioPortfolio::LoadPreview(&movie, fpfNil));
    path = base;
    CHECK(path.FAppendSz(PszLit(".bmp")));
    Filename texture;
    CHECK(texture.FBuildFromPath(&path));
    auto palette = DynamicArray::PglNew(sizeof(Color));
    CHECK(palette);
    for (int i = 0; i < 256; ++i)
    {
        Color color = {static_cast<byte>(i), static_cast<byte>(i), static_cast<byte>(i), 0};
        CHECK(palette->FPush(&color));
    }
    CHECK(FWriteBitmap(&texture, pixels, palette, 3, 2, true));
    ReleasePpo(&palette);
    // Movie probing may fail first; bitmap fallback must still work and leave
    // the caller's error stack and prior errors untouched.
    CheckPixels(StudioPortfolio::LoadPreview(&texture, fpfPortPrevMovie | fpfPortPrevTexture), pixels);
    CHECK(!StudioPortfolio::LoadPreview(&texture, fpfPortPrevMovie));
    path = base;
    CHECK(path.FAppendSz(PszLit(".missing")));
    CHECK(!StudioPortfolio::LoadPreviewPath(&path, fpfPortPrevMovie | fpfPortPrevTexture));
    CHECK(vpers == previous && vpers->Cerc() == errorCount);
    CHECK(vpers->FIn(ercFniDelete));
    CHECK(vpers->FPop());
    String name;
    name.SetSz(PszLit("movie.other"));
    CHECK(StudioPortfolio::AppendSaveExtension(&name, PszLit("3mm"), false));
    CHECK(name.FEqualSz(PszLit("movie.other")));
    CHECK(StudioPortfolio::AppendSaveExtension(&name, PszLit("3mm"), true));
    CHECK(name.FEqualSz(PszLit("movie.other.3mm")));
    StudioPortfolio::RemoveSaveExtension(&name);
    CHECK(name.FEqualSz(PszLit("movie.other")));
    name.SetSz(PszLit("movie"));
    StudioPortfolio::RemoveSaveExtension(&name);
    CHECK(name.FEqualSz(PszLit("movie")));
    achar longName[kcchMaxStn];
    for (auto &unit : longName)
        unit = ChLit('x');
    name.SetRgch(longName, kcchMaxStn);
    String unchanged = name;
    CHECK(!StudioPortfolio::AppendSaveExtension(&name, PszLit("3mm"), true));
    CHECK(name.FEqual(&unchanged));
    CHECK(movie.FDelete() && texture.FDelete());
    std::puts("Portfolio data: movie thumbnail pixels, BMP fallback, quiet failures, legacy save suffixes and "
              "transactional overflow passed without UI/SDK services.");
}
