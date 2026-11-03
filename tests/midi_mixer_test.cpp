/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "midi_stream_host.h"
#include "mididev2.h"
#include "midi.h"
#include "mdev2pri.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(FRAME_H)
#error The MIDI mixer test must link without the GUI or Windows SDK
#endif
#undef size
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
            std::fprintf(stderr, "MIDI mixer failed at %d\n", __LINE__);                                               \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
struct Buffer
{
    void *data;
    LegacyLong bytes, offset, repeats;
    std::uintptr_t tag;
};
class TestHost : public KauaiMidi::StreamHost
{
    KauaiMidi::Completion complete;
    std::uintptr_t user;
    std::mutex mutex;
    std::vector<Buffer> pending;
    std::vector<std::thread> returns;
    bool active = false;
    LegacyLong volume = kvlmFull;

  public:
    std::atomic<int> stops{0}, returned{0};
    TestHost(KauaiMidi::Completion complete, std::uintptr_t user) : complete(complete), user(user)
    {
    }
    ~TestHost()
    {
        for (auto &thread : returns)
            thread.join();
    }
    void SetVlm(LegacyLong value) override
    {
        volume = value;
    }
    LegacyLong VlmCur() override
    {
        return volume;
    }
    bool FActive() override
    {
        return active;
    }
    bool FActivate(bool value) override
    {
        active = value;
        return true;
    }
    const char *ProviderName() const override
    {
        return "test-borrowed-buffers";
    }
    bool FQueueBuffer(void *data, LegacyLong bytes, LegacyLong offset, LegacyLong repeats, std::uintptr_t tag) override
    {
        CHECK(active && bytes > offset && offset >= 0 && bytes % 12 == 0 && offset % 12 == 0);
        std::lock_guard<std::mutex> lock(mutex);
        pending.push_back({data, bytes, offset, repeats, tag});
        return true;
    }
    std::vector<Buffer> Pending()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return pending;
    }
    std::vector<Buffer> Take()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<Buffer> result;
        result.swap(pending);
        return result;
    }
    void Return(const std::vector<Buffer> &buffers)
    {
        for (const auto &buffer : buffers)
        {
            complete(user, buffer.data, buffer.tag);
            ++returned;
        }
    }
    void StopPlaying() override
    {
        ++stops;
        auto buffers = Take();
        // Return asynchronously so the mixer's stop path must wait correctly.
        returns.emplace_back([this, buffers] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            Return(buffers);
        });
    }
};
static TestHost *host;
static KauaiMidi::StreamHost *MakeHost(KauaiMidi::Completion complete, std::uintptr_t user)
{
    return host = NewObj TestHost(complete, user);
}
class MixerProbe : public MidiStreamMixer
{
  public:
    static MixerProbe *Make()
    {
        auto self = NewObj MixerProbe;
        if (self && !self->_FInit(MakeHost))
            ReleasePpo(&self);
        return self;
    }
    LegacyLong Voices()
    {
        _mutx.Enter();
        const auto count = _pglmsos->IvMac();
        _mutx.Leave();
        return count;
    }
};
static std::atomic<std::uint32_t> ticks{10000};
static std::uint32_t Ticks()
{
    return ticks.load();
}
static PMidiStreamCached Fixture(byte program, LegacyLong duration)
{
    std::vector<byte> raw{0, 0xC0, program, 0, 0x90, 60, 64};
    if (duration >= 128)
        raw.push_back(static_cast<byte>((duration >> 7) | 0x80));
    raw.push_back(static_cast<byte>(duration & 0x7F));
    raw.insert(raw.end(), {0x80, 60, 0, 0, 0xFF, 0x2F, 0});
    auto file = ChunkyFile::PcflCreateTemp();
    DataBlock block;
    ChunkNumber number;
    CHECK(file && file->FAdd(static_cast<LegacyLong>(raw.size()), 'MIDS', &number, &block));
    CHECK(block.FWriteRgb(raw.data(), static_cast<LegacyLong>(raw.size()), 0));
    auto stream = MidiStreamCached::PmdwsRead(&block);
    CHECK(stream && stream->Dts() == static_cast<LegacyUlong>(duration));
    LegacyLong bytes;
    auto events = static_cast<KauaiMidi::StreamEvent *>(stream->PvLockData(&bytes));
    CHECK(bytes == 5 * 12 && events[0].eventWord == ((KauaiMidi::tempo << 24) | 1000000) &&
          events[1].eventWord == (0xC0u | (std::uint32_t(program) << 8)) &&
          events[3].deltaTicks == std::uint32_t(duration));
    stream->UnlockData();
    block.Free();
    ReleasePpo(&file);
    return stream;
}
int main()
{
    auto previousClock = KauaiPlatform::SetTickSource(Ticks);
    CHECK(!MidiStreamPlayer::PmdpsNew(nullptr));
    auto player = MidiStreamPlayer::PmdpsNew(MakeHost);
    CHECK(player && host && host->FActive());
    player->Activate(false);
    ReleasePpo(&player);
    auto mixer = MixerProbe::Make();
    CHECK(mixer && host);
    auto first = MidiStreamQueue::PmsqueNew(mixer), second = MidiStreamQueue::PmsqueNew(mixer),
         wake = MidiStreamQueue::PmsqueNew(mixer);
    auto a = Fixture(5, 1000), b = Fixture(7, 200), c = Fixture(9, 1000);
    CHECK(first && second && wake && a && b && c);
    mixer->Suspend(false);
    CHECK(mixer->FPlay(first, a, 10, 10));
    CHECK(mixer->FPlay(second, b, 20, 5));
    CHECK(mixer->Voices() == 2 && host->Pending().size() == 1 &&
          host->Pending()[0].tag == reinterpret_cast<std::uintptr_t>(a));
    ticks += 250;
    CHECK(mixer->FPlay(wake));
    for (int i = 0; i < 200 && mixer->Voices() != 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(mixer->Voices() == 1);            // A lower-priority sound expires while A plays.
    CHECK(mixer->FPlay(second, c, 11, 10)); // Equal priority; newer sound wins.
    CHECK(host->stops == 1 && host->returned == 1 && mixer->Voices() == 2);
    CHECK(host->Pending().size() == 1 && host->Pending()[0].tag == reinterpret_cast<std::uintptr_t>(c));
    mixer->Suspend(true);
    CHECK(!host->FActive() && host->Pending().empty() && host->returned == 2);
    ticks += 100;
    mixer->Suspend(false);
    auto resumed = host->Pending();
    CHECK(resumed.size() == 2 && resumed[0].tag == 0 && resumed[1].tag == reinterpret_cast<std::uintptr_t>(c) &&
          resumed[1].offset > 0);
    host->Return(host->Take()); // Completing C resumes the interrupted A stream.
    auto restored = host->Pending();
    CHECK(mixer->Voices() == 1 && restored.size() == 2 && restored[0].tag == 0 &&
          restored[1].tag == reinterpret_cast<std::uintptr_t>(a) && restored[1].offset > 0);
    CHECK(mixer->FPlay(first) && mixer->Voices() == 0 && host->Pending().empty());
    mixer->Suspend(true);
    ReleasePpo(&wake);
    ReleasePpo(&second);
    ReleasePpo(&first);
    ReleasePpo(&a);
    ReleasePpo(&b);
    ReleasePpo(&c); // Checks all cached buffers were unlocked.
    ReleasePpo(&mixer);
    KauaiPlatform::SetTickSource(previousClock);
    std::puts("MIDI core: raw cache events, factory injection, priority/tie-break, background expiry, preemption, "
              "seek/resume and asynchronous stop passed.");
}
