/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_recording.h"
#include "sdl_audio.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include "soc.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
class RecordingApp : public ApplicationBase
{
  public:
#ifdef DEBUG
    bool FAssertProcApp(PSZS file, long line, PSZS message, void *, long) override
    {
        std::fprintf(stderr, "Recording assertion: %s:%ld: %s\n", file, line, message);
        std::exit(1);
    }
#endif
};
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Movie recording failed: %d\n", __LINE__);                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    auto app = NewObj RecordingApp;
    vpsndm = SoundManager::PsndmNew();
    CHECK(app && vpsndm);
    CHECK(!vpsndm->FSuspended());
    vpsndm->Suspend(true); // An outer modal/focus suspension must survive the recorder.
    CHECK(!SoundRecorder::PsrecNew(11025, 0, 1, 50));
    auto recorder = SoundRecorder::PsrecNew(11025, 1, 1, 50);
    CHECK(recorder && recorder->FStart());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (recorder->FRecording() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(!recorder->FRecording() && recorder->FHaveSound() && recorder->FPlay());
    CHECK(vpsndm->FSuspended());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(recorder->FPlaying());
    CHECK(recorder->FStop() && !recorder->FPlaying());
    CHECK(recorder->FPlay());
    vpsndm->Suspend(false);
    CHECK(!vpsndm->FSuspended());
    while (recorder->FPlaying() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(!recorder->FPlaying() && vpsndm->FSuspended());
    Filename filename;
    CHECK(filename.FGetTemp() && recorder->FSave(&filename));
    auto recorded = FileObject::PfilOpen(&filename);
    auto destination = ChunkyFile::PcflCreateTemp();
    CHECK(recorded && destination);
    recorded->SetTemp();
    ChunkNumber number;
    String name(PszLit("Recorded SDL regression sound"));
    CHECK(MovieSoundMSND::FCopyWave(recorded, destination, stySpeech, &number, &name));
    ChildChunkIdentification child;
    DataBlock block;
    CHECK(destination->FGetKidChidCtg(kctgMsnd, number, kchidSnd, kctgWave, &child) &&
          destination->FFind(child.cki.ctg, child.cki.cno, &block));
    std::vector<std::uint8_t> wave(block.Cb());
    CHECK(block.FReadRgb(wave.data(), static_cast<long>(wave.size()), 0));
    std::shared_ptr<const KauaiAudio::Sample> sample;
    CHECK(KauaiAudio::DecodeWave(wave.data(), wave.size(), sample) && !sample->stereo.empty());
    CHECK(KauaiAudio::RecorderStats().completed == 1 && KauaiAudio::RecorderStats().previewed == 2);
    CHECK(recorder->FStart() && !recorder->FHaveSound());
    // Destruction while recording must stop capture and balance manager suspend.
    ReleasePpo(&recorder);
    CHECK(!vpsndm->FSuspended());
    ReleasePpo(&recorded);
    ReleasePpo(&destination);
    ReleasePpo(&vpsndm);
    ReleasePpo(&app);
    std::puts("Studio recorder: original engine API, capture/preview controls, saved PCM WAVE, MSND import and "
              "recording cleanup passed.");
}
