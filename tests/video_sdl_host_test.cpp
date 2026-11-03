/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "video_sdl.h"
#include "video_stream_host.h"
#include "avi_fixture.h"
#include <cstdio>
#include <memory>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
class TestApp : public ApplicationBase
{
  public:
    GraphicsPort *EnsureDrawingPort()
    {
        RC bounds(0, 0, 8, 8);
        return _PgptEnsure(&bounds);
    }
};
#define CHECK(v)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(v))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL video host failed at %d: %s\n", __LINE__, #v);                                   \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    static TestApp app;
    _putenv_s("KAUAI_FONT_PROVIDER", "freetype");
    CHECK(vntl.FInit());
    CHECK(app.EnsureDrawingPort() && app.EnsureDrawingPort()->CbitPixel() == 8);
    const auto bytes = AviFixture::Fixture(true);
    auto file = std::fopen(argv[1], "wb");
    CHECK(file);
    CHECK(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    std::fclose(file);
    String path;
    path.SetSzs(argv[1]);
    Filename name;
    CHECK(name.FBuildFromPath(&path));
    RC bounds(0, 0, 8, 8);
    auto port = GraphicsPort::PgptNewOffscreen(&bounds, 32);
    CHECK(port);
    std::unique_ptr<KauaiVideo::StreamHost> host(KauaiVideo::NewSDLStreamHost(true));
    CHECK(host && host->Open(&name) && host->ControlsPlayback());
    CHECK(app.EnsureDrawingPort()->CbitPixel() == 32);
    CHECK(host->Info().frames == 3 && host->Info().width == 4);
    GraphicsEnvironment environment(port);
    for (int i : {0, 1, 2, 0})
    {
        CHECK(host->DrawFrame(i, &environment, &bounds));
        const auto p = reinterpret_cast<const std::uint32_t *>(port->PrgbLockPixels());
        for (int pixel = 0; pixel < 64; ++pixel)
            CHECK((p[pixel] & 0xFFFFFF) == (i ? 0x0B1621u : 0x07F807u));
        port->Unlock();
    }
    CHECK(!port->FHasNativeContext() && !port->FHasNativeMirror());
    CHECK(!host->DrawFrame(-1, &environment, &bounds) && !host->DrawFrame(3, &environment, &bounds));
    CHECK(host->Start(0) && host->Playing());
    host->Stop();
    CHECK(!host->Playing());
    CHECK(!host->Open(nullptr) && host->Info().frames == 0 && host->TimeForFrame(0) == -1);
    CHECK(app.EnsureDrawingPort()->CbitPixel() == 8);
    CHECK(host->Open(&name));
    host.reset();
    CHECK(app.EnsureDrawingPort()->CbitPixel() == 8);
    CHECK(name.FDelete());
    ReleasePpo(&port);
    std::puts("SDL video host: bounded file loading, cached frame seeks, actual portable GraphicsEnvironment pixels, "
              "no native mirrors, playback and released handles passed.");
}
