/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_audio.h"
#include "sdl_sound.h"
#include "wave_codec.h"
#include "wave_fixture.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include "soc.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
class ImportApp : public ApplicationBase
{
  public:
#ifdef DEBUG
    bool FAssertProcApp(PSZS file, long line, PSZS message, void *, long) override
    {
        std::fprintf(stderr, "Import assertion: %s:%ld: %s\n", file, line, message);
        std::exit(1);
    }
#endif
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Movie sound import failed: %d\n", __LINE__);                                         \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    auto app = NewObj ImportApp;
    auto input = FileObject::PfilCreateTemp();
    auto destination = ChunkyFile::PcflCreateTemp();
    CHECK(app && input && destination);
    const auto original = WaveFixture::Pcm(std::vector<std::int16_t>(48000, 8192));
    CHECK(input->FWriteRgb(const_cast<std::uint8_t *>(original.data()), static_cast<long>(original.size()), 0));
    String name(PszLit("Imported regression sound"));
    ChunkNumber number;
    CHECK(MovieSoundMSND::FCopyWave(input, destination, stySpeech, &number, &name));
    bool invalid = true;
    long type = -1, volume = -1;
    CHECK(MovieSoundMSND::FGetMsndInfo(destination, kctgMsnd, number, &invalid, &type, &volume));
    CHECK(!invalid && type == stySpeech && volume == kvlmFull);
    String storedName;
    CHECK(destination->FGetName(kctgMsnd, number, &storedName) && storedName.FEqual(&name));
    ChildChunkIdentification child;
    CHECK(destination->Ckid(kctgMsnd, number) == 1 &&
          destination->FGetKidChidCtg(kctgMsnd, number, kchidSnd, kctgWave, &child));
    DataBlock block;
    CHECK(destination->FFind(child.cki.ctg, child.cki.cno, &block));
    std::vector<std::uint8_t> imported(block.Cb());
    CHECK(block.FReadRgb(imported.data(), static_cast<long>(imported.size()), 0));
    CHECK(imported.size() < original.size() && KauaiAudio::LegacyWaveFormat(imported.data(), imported.size()));
    std::shared_ptr<const KauaiAudio::Sample> playback;
    CHECK(KauaiAudio::DecodeWave(imported.data(), imported.size(), playback) && playback->stereo.size() == 96000);
    const auto chunkCount = destination->Ccki();
    auto bad = FileObject::PfilCreateTemp();
    CHECK(bad);
    std::uint8_t junk[44] = {};
    CHECK(bad->FWriteRgb(junk, sizeof(junk), 0));
    ChunkNumber rejected = 123456;
    CHECK(!MovieSoundMSND::FCopyWave(bad, destination, stySfx, &rejected, &name));
    CHECK(destination->Ccki() == chunkCount && rejected == 123456);
    vpers->Clear();
    ReleasePpo(&bad);
    Filename persisted;
    CHECK(persisted.FGetTemp());
    CHECK(destination->FSave('Test', &persisted));
    destination->SetTemp(false);
    block.Free();
    ReleasePpo(&destination);
    destination = ChunkyFile::PcflOpen(&persisted, 0);
    CHECK(destination);
    destination->SetTemp(true);
    CHECK(destination->FGetKidChidCtg(kctgMsnd, number, kchidSnd, kctgWave, &child) &&
          destination->FFind(child.cki.ctg, child.cki.cno, &block));
    std::vector<std::uint8_t> reopened(block.Cb());
    CHECK(block.FReadRgb(reopened.data(), static_cast<long>(reopened.size()), 0) && reopened == imported);
    auto resources = ChunkyResourceFile::PcrfNew(destination, 1024 * 1024);
    CHECK(resources);
    auto sound = static_cast<PMovieSoundMSND>(resources->PbacoFetch(kctgMsnd, number, MovieSoundMSND::FReadMsnd));
    vpsndm = SoundManager::PsndmNew();
    auto device = KauaiAudio::NewWaveDevice();
    CHECK(sound && vpsndm && device && vpsndm->FAddDevice(kctgWave, device));
    ReleasePpo(&device);
    sound->Play(0, false, false, kvlmFull, 0, false, 900);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (vpsndm->FPlayingAll() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(!vpsndm->FPlayingAll() && KauaiAudio::StudioStats().started == 1 && KauaiAudio::StudioStats().completed == 1);
    ReleasePpo(&sound);
    ReleasePpo(&vpsndm);
    ReleasePpo(&resources);
    ReleasePpo(&input);
    ReleasePpo(&destination);
    ReleasePpo(&app);
    std::puts("Movie WAVE import: original engine path, legacy MSND metadata/name/WAVE child, compression, "
              "rejected-input rollback and persisted byte round-trip passed.");
}
