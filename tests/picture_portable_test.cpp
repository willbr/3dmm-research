/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain()
{
}
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%d: %s\n", file, static_cast<int>(line), message ? message : "");
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(2);
}
#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::fprintf(stderr, "check failed at %d: %s\n", __LINE__, #expression);                                   \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
int main()
{
    // Opaque data deliberately needs no EMF decoder. Preserve even payloads
    // that this runtime cannot interpret or display.
    const byte payload[] = {0, 0xff, 3, 1, 4, 0x80};
    RC bounds(5, 7, 8, 9);
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    PictureData stored;
    CHECK(stored.FInit(bounds, payload, sizeof(payload)));
    DataBlock input;
    ChunkNumber number;
    CHECK(file->FAdd(stored.CbOnFile(), 'META', &number, &input) && stored.FWrite(&input));
    auto picture = Picture::PpicRead(&input);
    CHECK(picture && picture->CbOnFile() == stored.CbOnFile());
    RC read;
    picture->GetRc(&read);
    CHECK(read == bounds);
    ChunkNumber parent;
    CHECK(picture->FAddToCfl(file, 'GRAF', &parent));
    auto loaded = Picture::PpicFetch(file, 'GRAF', parent);
    CHECK(loaded);
    DataBlock output;
    ChunkNumber copy;
    CHECK(file->FAdd(loaded->CbOnFile(), 'META', &copy, &output) && loaded->FWrite(&output));
    std::vector<byte> original(stored.CbOnFile()), rewritten(original.size());
    CHECK(input.FReadRgb(original.data(), static_cast<LegacyLong>(original.size()), 0));
    CHECK(output.FReadRgb(rewritten.data(), static_cast<LegacyLong>(rewritten.size()), 0));
    CHECK(original == rewritten);

    RC canvas(0, 0, 3, 2);
    auto port = GraphicsPort::PgptNewOffscreen(&canvas, 32);
    CHECK(port);
    const std::uint32_t pixels[] = {0x112233, 0x445566, 0x778899, 0xabcdef, 0x123456, 0x654321};
    auto buffer = port->PrgbLockPixels();
    std::memcpy(buffer, pixels, sizeof(pixels));
    port->Unlock();
    GraphicsDrawingData drawing{};
    auto destination = static_cast<SystemRectangle>(canvas);
    CHECK(vpers->Cerc() == 0);
    port->DrawPic(picture, &destination, &drawing);
    CHECK(vpers->FIn(ercGfxCantDraw));
    buffer = port->PrgbLockPixels();
    CHECK(!std::memcmp(buffer, pixels, sizeof(pixels)));
    port->Unlock();
    vpers->Clear();

    for (auto name : {"unsupported.emf", "unsupported.wmf", "unsupported.pict"})
    {
        String path;
        path.SetSzs(const_cast<char *>(name));
        Filename filename;
        CHECK(filename.FBuildFromPath(&path));
        CHECK(!Picture::PpicReadNative(&filename) && vpers->FIn(ercGfxCantDraw));
        vpers->Clear();
    }
    CHECK(!Picture::PpicNew(nullptr, &bounds) && vpers->FIn(ercGfxCantDraw));
    vpers->Clear();
    CHECK(!GraphicsPort::PgptNewPic(&canvas) && vpers->FIn(ercGfxCantDraw));
    vpers->Clear();
    // PpicRelease consumes its port even on an unsupported recording request.
    CHECK(!port->PpicRelease() && vpers->FIn(ercGfxCantDraw));
    port = nullptr;
    vpers->Clear();
    ReleasePpo(&loaded);
    ReleasePpo(&picture);
    ReleasePpo(&file);
    std::puts("Portable picture stubs: exact META roundtrip, explicit unsupported import/record/draw and unchanged "
              "pixels passed.");
}
