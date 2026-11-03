/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "midi_sdl.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error SDL MIDI test must not import the Windows SDK
#endif
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
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL MIDI failed at %d\n", __LINE__);                                                 \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
struct Results
{
    std::atomic<int> count{0};
    void *buffer = nullptr;
    std::uintptr_t tag = 0;
    static void Done(std::uintptr_t user, void *buffer, std::uintptr_t tag)
    {
        auto &self = *reinterpret_cast<Results *>(user);
        self.buffer = buffer;
        self.tag = tag;
        ++self.count;
    }
};
static void Wait(Results &result, int count)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (result.count < count && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(result.count == count);
}
int main()
{
    Results results;
    auto host = KauaiMidi::CreateSDLStreamHost(Results::Done, reinterpret_cast<std::uintptr_t>(&results));
    CHECK(host && !host->FActive() && host->FActivate(true));
    KauaiMidi::StreamEvent notes[] = {{0, 0, 0x00643C90}, {120, 0, 0x00003C80}};
    const auto tag = static_cast<std::uintptr_t>(UINT64_C(0xDDFF9977AACCEE11));
    host->SetVlm(kvlmFull / 2);
    CHECK(host->VlmCur() == kvlmFull / 2);
    CHECK(host->FQueueBuffer(notes, sizeof(notes), 0, 2, tag));
    Wait(results, 1);
    CHECK(results.buffer == notes && results.tag == tag);
    auto stats = KauaiMidi::StudioMidiStats();
    CHECK(stats.loaded && stats.active && stats.rendering.notes == 2 && stats.rendering.audibleFrames > 0 &&
          stats.rendering.completed == 1);
    KauaiMidi::StreamEvent mute[] = {{0, 0, 0x00000BB0}}; // Expression zero must not leak across device reopen.
    CHECK(host->FQueueBuffer(mute, sizeof(mute), 0, 1, tag));
    Wait(results, 2);
    CHECK(host->FQueueBuffer(notes, sizeof(notes), 0, 0, tag));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    host->StopPlaying();
    Wait(results, 3);
    CHECK(host->FActivate(false) && !host->FActive());
    const auto beforeReopen = KauaiMidi::StudioMidiStats().rendering.audibleFrames;
    CHECK(host->FActivate(true) && host->FQueueBuffer(notes, sizeof(notes), 0, 1, tag));
    Wait(results, 4);
    CHECK(KauaiMidi::StudioMidiStats().rendering.audibleFrames > beforeReopen);
    CHECK(host->FActivate(false));
    ReleasePpo(&host);
    CHECK(!KauaiMidi::StudioMidiStats().active);
    std::puts("SDL MIDI: real bank PCM rendering, dummy-device playback, repeats, full-width completion, stop and "
              "reopen passed.");
}
