/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
#include <string>
#include "midi_stream_host.h"
#include "frame.h"
#include "midi_stream_win.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
static void Check(bool ok, const char *message)
{
    if (!ok)
    {
        std::fprintf(stderr, "MIDI stream host: %s\n", message);
        std::exit(1);
    }
}
struct Returned
{
    void *buffer;
    std::uintptr_t tag;
    std::chrono::steady_clock::time_point time;
};
struct Callbacks
{
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Returned> returned;
    static void Complete(std::uintptr_t user, void *buffer, std::uintptr_t tag)
    {
        auto &self = *reinterpret_cast<Callbacks *>(user);
        std::lock_guard<std::mutex> lock(self.mutex);
        self.returned.push_back({buffer, tag, std::chrono::steady_clock::now()});
        self.changed.notify_all();
    }
    void Wait(std::size_t count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        Check(changed.wait_for(lock, std::chrono::seconds(3), [&] { return returned.size() >= count; }),
              "timed out waiting for borrowed buffers");
    }
};
int main()
{
    if (!midiOutGetNumDevs())
    {
        std::puts("MIDI stream host requires an installed Windows MIDI output.");
        return 77;
    }
    Callbacks callbacks;
    auto host = KauaiMidi::CreateStreamHost(Callbacks::Complete, reinterpret_cast<std::uintptr_t>(&callbacks));
    Check(host && !host->FActive(), "could not create an inactive host");
    const std::string provider = host->ProviderName();
    Check(host->FActivate(true) && host->FActive(), "could not activate the MIDI mapper");
    host->SetVlm(kvlmFull / 2);
    Check(host->VlmCur() == kvlmFull / 2, "relative volume did not round-trip");
    host->SetVlm(kvlmFull);
    KauaiMidi::StreamEvent repeated[] = {{0, 0, (KauaiMidi::tempo << 24) | 1000000},
                                         {150, 0, KauaiMidi::noOperation << 24}};
    KauaiMidi::StreamEvent offset[] = {{10000, 0, KauaiMidi::noOperation << 24}, {40, 0, KauaiMidi::noOperation << 24}};
    const auto firstTag = static_cast<std::uintptr_t>(UINT64_C(0xDDBB7755AACCEE11));
    const auto secondTag = static_cast<std::uintptr_t>(UINT64_C(0xEECC8866BBDDFF22));
    const auto started = std::chrono::steady_clock::now();
    Check(host->FQueueBuffer(repeated, sizeof(repeated), 0, 2, firstTag), "could not queue a repeated buffer");
    Check(host->FQueueBuffer(offset, sizeof(offset), sizeof(KauaiMidi::StreamEvent), 1, secondTag),
          "could not queue a buffer with an offset");
    callbacks.Wait(2);
    {
        std::lock_guard<std::mutex> lock(callbacks.mutex);
        Check(callbacks.returned.size() == 2 && callbacks.returned[0].buffer == repeated &&
                  callbacks.returned[0].tag == firstTag && callbacks.returned[1].buffer == offset &&
                  callbacks.returned[1].tag == secondTag,
              "buffer order or pointer-sized callback values changed");
        Check(callbacks.returned[0].time - started >= std::chrono::milliseconds(225),
              "buffer returned before its second pass completed");
    }
    Check(host->FQueueBuffer(repeated, sizeof(repeated), 0, klwMax, secondTag), "could not queue a looping buffer");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    host->StopPlaying();
    callbacks.Wait(3);
    Check(host->FActivate(false) && !host->FActive(), "could not deactivate after stop");
    ReleasePpo(&host); // Joins the callback thread before releasing callback state.
    Check(callbacks.returned.size() == 3 && callbacks.returned[2].buffer == repeated &&
              callbacks.returned[2].tag == secondTag,
          "stopped buffer did not return exactly once");

    // Exercise the actual WinMM header ABI even on modern Windows, where the
    // original factory chooses its scheduled-output fallback instead of the
    // Win95 stream implementation. No note messages or long messages are sent.
    HMIDIOUT device = nullptr;
    Check(midiOutOpen(&device, MIDI_MAPPER, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR,
          "could not open header ABI probe");
    MH header = {};
    header.lpData = reinterpret_cast<LPSTR>(repeated);
    header.dwBufferLength = sizeof(repeated);
    header.dwUser = firstTag;
    Check(midiOutPrepareHeader(device, &header, sizeof(header)) == MMSYSERR_NOERROR,
          "WinMM rejected the native MIDIHDR layout");
    Check(header.dwUser == firstTag, "native MIDIHDR truncated pointer-sized user data");
    Check(midiOutUnprepareHeader(device, &header, sizeof(header)) == MMSYSERR_NOERROR,
          "could not unprepare native MIDIHDR");
    Check(midiOutClose(device) == MMSYSERR_NOERROR, "could not close header ABI probe");
    std::printf(
        "MIDI stream host (%s): FIFO, repeats, offsets, stop, full-width callback tags and native header ABI passed.\n",
        provider.c_str());
}
