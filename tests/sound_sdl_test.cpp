/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_sound.h"
#include "wave_fixture.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
class SoundApp : public ApplicationBase
{
  public:
    void Focus(bool active)
    {
        _Activate(active);
    }
#ifdef DEBUG
    bool FAssertProcApp(PSZS file, long line, PSZS message, void *, long) override
    {
        std::fprintf(stderr, "Sound assertion: %s:%ld: %s\n", file, line, message);
        std::exit(1);
    }
#endif
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Kauai SDL sound failed: %d\n", __LINE__);                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static bool Wait(PSoundDevice device, long id)
{
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (device->FPlaying(id) && std::chrono::steady_clock::now() < limit)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return !device->FPlaying(id);
}
int main()
{
    auto app = NewObj SoundApp;
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(app && file);
    auto bytes = WaveFixture::Pcm(std::vector<std::int16_t>(4800, 1000));
    ChunkNumber number;
    DataBlock block;
    CHECK(file->FAdd(static_cast<long>(bytes.size()), kctgWave, &number, &block) &&
          block.FWriteRgb(bytes.data(), static_cast<long>(bytes.size()), 0));
    CHECK(file->FPackData(kctgWave, number) && file->FPacked(kctgWave, number));
    auto resources = ChunkyResourceFile::PcrfNew(file, 1024 * 1024);
    auto emptyFile = ChunkyFile::PcflCreateTemp();
    CHECK(resources && emptyFile);
    auto empty = ChunkyResourceFile::PcrfNew(emptyFile, 0);
    auto typed = mm_resource_cache_typed_sibling(resources->CoreCache(), 1024 * 1024);
    auto catalog = mm_resource_manager_create(nullptr, nullptr);
    CHECK(empty && typed && catalog);
    CHECK(mm_resource_manager_add_cache(catalog, empty->CoreCache(), nullptr) == MM_STORAGE_OK &&
          mm_resource_manager_add_cache(catalog, typed, nullptr) == MM_STORAGE_OK);
    ReleasePpo(&empty);
    ReleasePpo(&emptyFile);
    ReleasePpo(&resources);
    mm_resource_cache_release(typed);
    const MMResourceAccess single = {typed, nullptr};
    const MMResourceAccess ordered = {nullptr, catalog};
    auto device = KauaiAudio::NewWaveDevice();
    CHECK(device && KauaiAudio::StudioStats().active);
    device->BeginSynch();
    const auto missing = device->SiiPlay(ordered, kctgWave, number + 1, 7);
    CHECK(!device->FPlaying(missing) && !KauaiAudio::StudioStats().decoded);
    const auto first = device->SiiPlay(ordered, kctgWave, number, 7);
    const auto second = device->SiiPlay(single, kctgWave, number, 7, kvlmFull / 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(device->FPlaying(first) && device->FPlaying(second) && !KauaiAudio::StudioStats().started);
    device->EndSynch();
    CHECK(Wait(device, first) && Wait(device, second));
    auto paused = device->SiiPlay(single, kctgWave, number, 8, kvlmFull, 1, 0, 0, 42);
    device->Pause(paused);
    device->Pause(paused);
    device->Resume(paused);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(device->FPlaying(paused));
    device->Resume(paused);
    CHECK(Wait(device, paused));
    const auto started = KauaiAudio::StudioStats().started;
    auto beyondEnd = device->SiiPlay(single, kctgWave, number, 8, kvlmFull, 1, 200);
    CHECK(Wait(device, beyondEnd) && KauaiAudio::StudioStats().started == started);
    auto looping = device->SiiPlay(single, kctgWave, number, 8, kvlmFull, 0, 0, 0, 42);
    device->Activate(false);
    CHECK(device->FPlaying(looping));
    device->Activate(true);
    device->StopAll(8, 41);
    CHECK(device->FPlaying(looping));
    device->StopAll(8, 42);
    CHECK(!device->FPlaying(looping));
    device->SetVlm(kvlmFull / 2);
    CHECK(device->VlmCur() == kvlmFull / 2);
    CHECK(KauaiAudio::StudioStats().decoded == 1 && KauaiAudio::StudioStats().started >= 2);

    auto manager = SoundManager::PsndmNew();
    CHECK(manager && manager->FAddDevice(kctgWave, device));
    vpsndm = manager;
    manager->SetSoundClassRange(0, 2);
    manager->BeginSynch();
    for (const auto soundClass : {-1, 0, 1, 2, 998, 10000})
    {
        const auto id = manager->SiiPlay(ordered, kctgWave, number, 9, kvlmFull, 1, 0, 0, soundClass);
        CHECK(manager->FPlaying(id) == (soundClass >= 0 && soundClass <= 2));
    }
    manager->EndSynch();
    manager->StopAll();
    manager->SetSoundClassRange(klwMin, klwMax);
    const auto ui = manager->SiiPlay(ordered, kctgWave, number, 9, kvlmFull, 0, 0, 0, 10000);
    CHECK(manager->FPlaying(ui));
    app->SetBackgroundPlayback(true);
    app->Focus(false);
    CHECK(!app->FForeground() && manager->FActive());
    app->SetBackgroundPlayback(false);
    CHECK(!manager->FActive());
    app->Focus(true);
    CHECK(app->FForeground() && manager->FActive());
    manager->StopAll();
    vpsndm = nullptr;
    ReleasePpo(&manager);
    ReleasePpo(&device);
    CHECK(!KauaiAudio::StudioStats().active);
    mm_resource_manager_destroy(catalog);
    ReleasePpo(&file);
    ReleasePpo(&app);
    std::puts("Kauai SDL sound: packed chunky WAVE cache, queue chaining, grouped starts, nested pause, activation, "
              "class stop and teardown passed.");
}
