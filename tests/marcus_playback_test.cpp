#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include "player.h"
#include "waveform.h"
#include "sound_timeline.h"
#include "playback.h"
#include "timing_job.h"
#include "retail_scroll.h"
#include "soc_core.h"
#include <SDL3/SDL.h>
#include <limits>
#undef size
ASSERTNAME
namespace
{
MarcusPlayer *player = nullptr;
void Check(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "Playback check line %d: %s\n", line, marcus_error(player));
        std::exit(1);
    }
}
#define CHECK(value) Check((value), __LINE__)
void BackgroundTiming()
{
    MarcusMovieTime time;
    MarcusInfo before, after;
    MarcusAudioStats audioBefore, audioAfter;
    CHECK(marcus_info(player, &before));
    CHECK(marcus_audio_stats(player, &audioBefore));
    int status = marcus_movie_time_async(player, &time);
    CHECK(status == 0); // Starts the job without scanning the remaining scenes.
    CHECK(marcus_seek(player, before.first_frame)); // Live scene remains usable.
    MarcusImage image;
    CHECK(marcus_render(player, 128, 96, 1, 0, 0, 1, &image));
    const auto deadline = SDL_GetTicks() + 15000;
    while (!status && SDL_GetTicks() < deadline)
    {
        SDL_Delay(5);
        status = marcus_movie_time_async(player, &time);
    }
    CHECK(status == 1);
    CHECK(marcus_info(player, &after));
    CHECK(after.scene == before.scene && after.frame == before.first_frame);
    CHECK(after.sound_count == before.sound_count && after.sound_track_count == before.sound_track_count);
    CHECK(marcus_audio_stats(player, &audioAfter));
    CHECK(audioBefore.wave_sounds == audioAfter.wave_sounds && audioBefore.midi_sounds == audioAfter.midi_sounds);
}
void FailedTiming()
{
    auto input = std::make_shared<MarcusMovieFile>("missing-timing-movie.3mm");
    MarcusTimingJob job(input, "missing-timing-resources.chk", "", {});
    std::unique_ptr<MarcusSoundTimeline> result;
    std::string error;
    const auto deadline = SDL_GetTicks() + 15000;
    bool done = false;
    while (!(done = job.Take(result, error)) && SDL_GetTicks() < deadline)
        SDL_Delay(5);
    CHECK(done && !result && !error.empty());
}
void RetailScrollSteps()
{
    RetailScrollPlayback clock;
    auto update = clock.Update(10000, true);
    CHECK(update.holding && !update.step); // Entry arms an alarm, not a jump.
    CHECK(!clock.Update(83, true).step);
    CHECK(clock.Update(1, true).step);
    update = clock.Update(10000, true);
    CHECK(update.step && update.holding); // A late callback moves one pixel.
    CHECK(!clock.Update(0, true).step); // No accumulated catch-up debt.
    CHECK(!clock.Update(83, true).step);
    CHECK(clock.Update(1, true).step);
    update = clock.Update(0, false); // Final pixel still has a pending alarm.
    CHECK(update.holding && !update.step);
    CHECK(clock.Update(83, false).holding);
    update = clock.Update(1, false);
    CHECK(!update.holding && !update.step);
    CHECK(!clock.Update(10000, true).step); // Completed frame stays completed.
    clock.Reset();
    CHECK(!clock.Update(0, false).holding); // No overflow requires no hold.
    clock.Reset();
    CHECK(clock.Update(0, true).holding);
    CHECK(clock.Update(84, true).step); // Rewind starts at the first step.
    CHECK(std::abs(RetailScrollPlayback::NominalSeconds(3) - 4.0 / 12) < 1e-9);
    CHECK(RetailScrollPlayback::NominalSeconds(0) == 0);
}
void WaveformPeaks()
{
    // Preserve transients and opposing stereo channels at both coarse and
    // zoomed-in scales. Tail padding and out-of-clip windows stay silent.
    std::vector<float> samples(1025 * 2, 0);
    samples[256 * 2] = .75f;
    samples[256 * 2 + 1] = -.5f;
    samples[1024 * 2 + 1] = -1;
    MarcusWaveformEnvelope envelope;
    envelope.Build(samples.data(), 1025);
    MarcusWaveformColumn columns[6];
    envelope.Read(0, 256.0 / 48000, 6, columns);
    CHECK(columns[0].minimum == 0 && columns[0].maximum == 0);
    CHECK(columns[1].minimum == -.5f && columns[1].maximum == .75f);
    CHECK(columns[2].minimum == 0 && columns[2].maximum == 0);
    CHECK(columns[4].minimum == -1 && columns[4].maximum == 0);
    CHECK(columns[5].minimum == 0 && columns[5].maximum == 0);
    envelope.Read(256.0 / 48000, 1.0 / 48000, 1, columns);
    CHECK(columns[0].minimum == -.5f && columns[0].maximum == .75f);
    envelope.Read(0, 1, 1, columns);
    CHECK(columns[0].minimum == -1 && columns[0].maximum == .75f);
    envelope.Read(envelope.Seconds() * 2, 1, 1, columns, true);
    CHECK(columns[0].minimum == -1 && columns[0].maximum == .75f);
    CHECK(std::abs(envelope.Seconds() - 1025.0 / 48000) < 1e-9);
    samples.assign(256 * 2, 0);
    samples[0] = .5f;
    samples.back() = -1;
    envelope.Build(samples.data(), 256);
    envelope.Read(255.0 / 48000, 2.0 / 48000, 1, columns, true);
    CHECK(columns[0].minimum == -1 && columns[0].maximum == .5f);
    envelope.Build(nullptr, 0);
    envelope.Read(0, 1, 6, columns);
    for (const auto &column : columns)
        CHECK(column.minimum == 0 && column.maximum == 0);

    ThreeMM::Preview unopened;
    ThreeMM::PreviewSoundCue cue;
    MarcusWaveformCache cache;
    MarcusWaveformInfo info;
    cue.silent = true;
    cache.Read(unopened, cue, 0, 1, 6, columns, info);
    CHECK(info.kind == MARCUS_WAVEFORM_SILENT);
    cue.silent = false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        cache.Read(unopened, cue, 0, 1, 6, columns, info);
        CHECK(info.kind == MARCUS_WAVEFORM_UNAVAILABLE && info.message[0]);
        for (const auto &column : columns)
            CHECK(column.minimum == 0 && column.maximum == 0);
    }
}
std::vector<byte> Bytes(const char *path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Word(std::vector<byte> &bytes, std::uint32_t value, int count)
{
    for (int i = 0; i < count; ++i)
        bytes.push_back(static_cast<byte>(value >> (i * 8)));
}
ChunkNumber Sound(PChunkyFile file, bool midi, unsigned samples = 4800)
{
    struct Header
    {
        int16_t bo, osk;
        int32_t style, volume, invalid;
    } header = {kboCur, koskCur, midi ? styMidi : stySfx, 0x10000, 0};
    ChunkNumber sound, child;
    CHECK(file->FAddPv(&header, sizeof(header), kctgMsnd, &sound));
    std::vector<byte> bytes;
    if (midi)
    {
        // MIDS is the engine's millisecond event stream, not a Standard MIDI file.
        const byte events[] = {0, 0xc0, 0, 0, 0x90, 60, 110, 100, 0x80, 60, 0, 0, 0xff, 0x2f, 0};
        bytes.assign(events, events + sizeof(events));
    }
    else
    {
        bytes.insert(bytes.end(), {'R', 'I', 'F', 'F'});
        Word(bytes, 36 + samples * 2, 4);
        bytes.insert(bytes.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        Word(bytes, 16, 4);
        Word(bytes, 1, 2);
        Word(bytes, 1, 2);
        Word(bytes, 48000, 4);
        Word(bytes, 96000, 4);
        Word(bytes, 2, 2);
        Word(bytes, 16, 2);
        bytes.insert(bytes.end(), {'d', 'a', 't', 'a'});
        Word(bytes, samples * 2, 4);
        for (unsigned i = 0; i < samples; ++i)
            Word(bytes, static_cast<std::uint16_t>(std::int16_t(8000 * std::sin(i * .057596))), 2);
    }
    CHECK(file->FAddPv(bytes.data(), static_cast<LegacyLong>(bytes.size()), midi ? kctgMidi : kctgWave, &child));
    CHECK(file->FAdoptChild(kctgMsnd, sound, midi ? kctgMidi : kctgWave, child, 0));
    return sound;
}
void CarryScheduling()
{
    MarcusSoundTimeline timeline;
    MarcusScene first = {}, later = {};
    first.first_frame = 10;
    first.last_frame = 15;
    later.first_frame = -5;
    later.last_frame = 0;
    auto read = [](const ThreeMM::PreviewSoundCue &cue) {
        MarcusWaveformInfo audio = {};
        audio.kind = cue.silent ? MARCUS_WAVEFORM_SILENT : cue.style == MARCUS_SOUND_MUSIC
                     ? MARCUS_WAVEFORM_MIDI : MARCUS_WAVEFORM_SAMPLED;
        audio.seconds = cue.sound.number == 4 ? .25 : cue.sound.number == 5 ? 1 : 2;
        audio.resolved_chunk = cue.sound.number;
        return audio;
    };
    ThreeMM::PreviewSoundCue music, effect, speech, loop;
    music.frame = 10; music.style = MARCUS_SOUND_MUSIC; music.loop = true; music.sound.number = 100;
    effect.frame = 13; effect.style = MARCUS_SOUND_EFFECT; effect.sound.number = 1;
    speech.frame = 14; speech.style = MARCUS_SOUND_SPEECH; speech.actorSound = true;
    speech.actor = 7; speech.sound.number = 2;
    loop = effect; loop.frame = 10; loop.loop = true; loop.actorSound = true; loop.actor = 9;
    timeline.Append(first, {music, loop, effect, speech}, read);
    music.frame = -3;
    timeline.Append(later, {music}, read);
    CHECK(timeline.Scene(1).sounds.size() == 4); // Three tails plus the duplicate music marker.
    CHECK(timeline.Scene(1).sounds[0]->scene == 0 && timeline.Scene(1).sounds[0]->start == 0);
    CHECK(std::abs(timeline.Scene(1).start - timeline.Scene(1).sounds[1]->start - .5) < 1e-6);
    CHECK(timeline.Scene(0).sounds[1]->end == 1); // Sampled loops stop at the cut.
    effect.frame = -5; effect.sound.number = 4;
    speech.frame = -5; speech.sound.number = 5; speech.queue = true;
    timeline.Append(later, {effect, speech}, read);
    CHECK(timeline.Scene(2).sounds.size() == 4); // Music, speech tail, replacement, queued speech.
    CHECK(timeline.Scene(0).sounds[2]->end == 2); // Replacement truncates the carried effect.
    CHECK(std::abs(timeline.Scene(2).sounds.back()->start - 8.0 / 3) < 1e-6);
    const auto queuedResume = timeline.Resume(2, -3);
    CHECK(queuedResume.size() == 3 && queuedResume.back().cue.queue && queuedResume.back().seconds == 0);
    timeline.Append(later, {}, read);
    CHECK(timeline.Scene(3).sounds.size() == 2); // Only music and the queued speech remain.
    music.frame = -5; music.silent = true;
    timeline.Append(later, {music}, read);
    CHECK(timeline.Scene(4).sounds.size() == 1 && timeline.Scene(4).sounds[0]->cue.silent);
    std::ostringstream encoded(std::ios::binary);
    CHECK(timeline.Write(encoded));
    MarcusSoundTimeline imported;
    const std::vector<MarcusScene> expected = {first, later, later, later, later};
    std::istringstream complete(encoded.str(), std::ios::binary);
    CHECK(imported.Read(complete, expected) && imported.Size() == timeline.Size());
    for (std::size_t i = 0; i < imported.Size(); ++i)
    {
        CHECK(imported.Scene(i).start == timeline.Scene(i).start);
        CHECK(imported.Scene(i).sounds.size() == timeline.Scene(i).sounds.size());
        for (std::size_t j = 0; j < imported.Scene(i).sounds.size(); ++j)
        {
            CHECK(imported.Scene(i).sounds[j]->start == timeline.Scene(i).sounds[j]->start);
            CHECK(imported.Scene(i).sounds[j]->end == timeline.Scene(i).sounds[j]->end);
        }
        const auto original = timeline.Resume(i, later.first_frame);
        const auto restored = imported.Resume(i, later.first_frame);
        CHECK(original.size() == restored.size());
        for (std::size_t j = 0; j < original.size(); ++j)
            CHECK(original[j].seconds == restored[j].seconds && original[j].cue.queue == restored[j].cue.queue);
    }
    for (std::size_t length : {std::size_t(0), std::size_t(7), encoded.str().size() - 1})
    {
        std::istringstream truncated(encoded.str().substr(0, length), std::ios::binary);
        CHECK(!imported.Read(truncated, expected) && imported.Size() == expected.size());
    }
    std::istringstream trailing(encoded.str() + "x", std::ios::binary);
    CHECK(!imported.Read(trailing, expected));
    timeline.Clear();
    loop.actorSound = false; loop.frame = 10;
    effect = loop; effect.loop = false; effect.queue = true; effect.sound.number = 5;
    timeline.Append(first, {loop, effect}, read);
    timeline.Append(later, {}, read);
    CHECK(timeline.Scene(1).sounds.size() == 1 && timeline.Scene(1).sounds[0]->cue.queue);
    CHECK(timeline.Scene(1).sounds[0]->start == 1 && timeline.Scene(1).sounds[0]->end == 2);
}
void PresentationClock()
{
    MarcusScene first = {}, later = {};
    first.first_frame = 10; first.last_frame = 15;
    later.first_frame = -5; later.last_frame = 0;
    ThreeMM::PreviewSoundCue cue;
    cue.frame = 10; cue.style = MARCUS_SOUND_EFFECT;
    auto read = [](const ThreeMM::PreviewSoundCue &) {
        MarcusWaveformInfo audio = {};
        audio.kind = MARCUS_WAVEFORM_SAMPLED; audio.seconds = 20;
        return audio;
    };
    MarcusSoundTiming timing;
    timing.scrolling[11] = 2;
    timing.pauses.push_back({12, 3, 500});
    timing.transition = 4;
    MarcusSoundTimeline timeline;
    timeline.Append(first, {cue}, read, timing);
    const auto &scene = timeline.Scene(0);
    CHECK(std::abs(scene.TimeAt(12) - 7.0 / 3) < 1e-6);
    CHECK(std::abs(scene.FrameAt(scene.TimeAt(11.5)) - 11.5) < 1e-6);
    CHECK(std::abs(scene.TimeAt(16) - 3.5) < 1e-6);
    timeline.Append(later, {}, read);
    CHECK(std::abs(timeline.Scene(1).start - 7.5) < 1e-6);
    const auto resume = timeline.Resume(1, -3);
    CHECK(resume.size() == 1 && std::abs(resume[0].seconds - 47.0 / 6) < 1e-6);
    timeline.Clear();
    timing.transition = 0;
    timing.pauses.push_back({13, 2, 0});
    timeline.Append(first, {cue}, read, timing);
    CHECK(std::abs(timeline.Scene(0).TimeAt(16) - 20.5) < 1e-6);
    timeline.Append(later, {}, read);
    CHECK(timeline.Resume(1, -5).empty()); // Sound-wait audio has completed.
    ThreeMM::PreviewPresentation presentation;
    ThreeMM::Error error;
    for (int transition = 0; transition <= 5; ++transition)
    {
        presentation.transition = transition;
        CHECK(MarcusPlayback::ReadTiming(presentation, timing, error));
        const double expected = transition == 4 ? 1.0 / 6 : transition > 0 && transition < 4 ? 4 : 0;
        CHECK(timing.transition == expected);
    }
}
ChunkNumber Caption(PChunkyFile file, bool scrolling)
{
    RichTextData text;
    text.fontName.SetSzs(PszLit("Arial"));
    text.defaults.dypFont = 18;
    text.defaults.dxpDef = 400;
    text.defaults.lwAcrBack = scrolling ? 0x01000000 : 0x01ffffff;
    text.textOsk = koskUniWin;
    const std::u16string content = scrolling ? u"Credits one\rCredits two\rCredits three\rCredits four\rCredits five"
                                             : u"Marcus captions \u00e9\u03a9\rA second line";
    text.text.pfil = FileObject::PfilCreateTemp();
    text.text.cb = static_cast<LegacyLong>(content.size() * 2);
    CHECK(text.text.pfil && text.text.pfil->FSetFpMac(text.text.cb));
    CHECK(text.text.FWriteRgb(const_cast<char16_t *>(content.data()), text.text.cb, 0));
    text.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
    CHECK(text.properties);
    RichTextPropertyEntry style = {0x01000000, 1}; // Bold.
    RichTextPropertyEntry foreground = {0x03000000, scrolling ? 0xffffff : 0x0000ff};
    RichTextPropertyEntry alignment = {0x80000000, 0x02000000}; // Centered.
    CHECK(text.properties->FAdd(&style) && text.properties->FAdd(&foreground) && text.properties->FAdd(&alignment));
    ChunkIdentification rich;
    CHECK(text.FWrite(file, &rich));
    TextBoxData box;
    box.bounds.Set(20, 20, 460, scrolling ? 55 : 110);
    box.firstFrame = scrolling ? 0 : -2;
    box.maxFrame = scrolling ? 5 : -1;
    box.story = !scrolling;
    ChunkNumber number;
    CHECK(file->FAdd(0, kctgTbox, &number) && box.FWrite(file, number));
    CHECK(file->FAdoptChild(kctgTbox, number, rich.ctg, rich.cno, 0));
    return number;
}
void SceneSound(PGeneralGroup events, int frame, ChunkNumber sound, int slot, bool midi, bool loop = false,
                bool queued = false, int style = styNil)
{
    SceneEvent event = {frame, sevtPlaySnd};
    std::int32_t data[] = {0x10000,
                           midi ? styMidi : style == styNil ? stySfx : style,
                           loop ? 1 : 0,
                           queued ? 2 : 1,
                           slot,
                           0,
                           0,
                           kctgMsnd,
                           static_cast<std::int32_t>(sound),
                           slot,
                           0,
                           0,
                           kctgMsnd,
                           static_cast<std::int32_t>(sound)};
    CHECK(events->FInsert(events->IvMac(), queued ? sizeof(data) : 36, data, &event));
}
void Actor(PChunkyFile file, ChunkNumber scene, ChunkNumber sound, PGeneralGroup starts)
{
    ActorChunkOnFile header = {};
    header.bo = kboCur;
    header.osk = koskCur;
    header.arid = 42;
    header.nfrmFirst = -2;
    header.nfrmLast = 4;
    header.tagTmpl = {2, 0, kctgTmpl, 0x2010};
    ChunkNumber number, child;
    CHECK(file->FAdd(0, kctgActr, &number) && ActorData::FWriteHeader(file, number, &header));
    CHECK(file->FAdoptChild(kctgScen, scene, kctgActr, number, 0));
    ActorTrack track;
    CHECK(track.FCreate());
    RouteDistancePoint point = {};
    CHECK(track.route->FAdd(&point));
    ActorEvent::Base event = {ActorEvent::aetAdd, -2, {0, rZero, 0}};
    ActorEvent::Add add = {};
    CHECK(track.events->FInsert(track.events->IvMac(), sizeof(add), &add, &event));
    event.aet = ActorEvent::aetActn;
    ActorEvent::Action action = {0, 0};
    CHECK(track.events->FInsert(track.events->IvMac(), sizeof(action), &action, &event));
    event.aet = ActorEvent::aetStep;
    BRS step = 0;
    CHECK(track.events->FInsert(track.events->IvMac(), sizeof(step), &step, &event));
    event.aet = ActorEvent::aetFreeze;
    std::int32_t frozen = 1;
    CHECK(track.events->FInsert(track.events->IvMac(), sizeof(frozen), &frozen, &event));
    event.aet = ActorEvent::aetSnd;
    ActorEvent::Sound cue = {};
    cue.sty = stySfx;
    cue.vlm = 0x10000;
    cue.celn = 0;
    cue.chid = 7;
    cue.tag = {0, nullptr, kctgMsnd, sound + 123}; // Resolve imported sound through SCEN child 7.
    CHECK(track.events->FInsert(track.events->IvMac(), sizeof(cue), &cue, &event));
    cue.celn = ivNil;
    CHECK(track.events->FInsert(track.events->IvMac(), sizeof(cue), &cue, &event));
    DataBlock block;
    CHECK(file->FAddChild(kctgActr, number, 0, track.route->CbOnFile(), kctgPath, &child, &block));
    CHECK(track.route->FWrite(&block));
    auto wire = ActorData::PggWriteEvents(track.events);
    CHECK(wire && file->FAddChild(kctgActr, number, 0, wire->CbOnFile(), kctgGgae, &child, &block));
    CHECK(wire->FWrite(&block));
    ReleasePpo(&wire);
    std::uint32_t slot = 0;
    SceneEvent start = {-2, sevtAddActr};
    CHECK(starts->FInsert(starts->IvMac(), sizeof(slot), &slot, &start));
}
void InvalidMovieHeaders(const char *path, const char *resources, const char *content)
{
    const std::string invalidPath = std::string(path) + ".invalid-header.3mm";
    String native;
    native.SetSzs(const_cast<char *>(invalidPath.c_str()));
    Filename filename;
    CHECK(filename.FBuildFromPath(&native));
    for (int variant = 0; variant < 2; ++variant)
    {
        auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
        CHECK(file);
        ChunkNumber root;
        DataBlock block;
        CHECK(MovieData::FCreateRoot(file, &root) && file->FFind(kctgMvie, root, &block));
        MovieFilePrefix prefix;
        CHECK(block.FReadRgb(&prefix, sizeof(prefix), 0));
        if (variant == 0)
            prefix.dver.Set(2, 3);
        else
            prefix.bo = 2;
        CHECK(block.FWriteRgb(&prefix, sizeof(prefix), 0) && file->FSave(kctgSoc));
        block.Free();
        ReleasePpo(&file);
        auto probe = marcus_create();
        CHECK(probe && !marcus_open(probe, invalidPath.c_str(), resources, content));
        if (!std::strstr(marcus_error(probe), "record header"))
            std::fprintf(stderr, "Invalid-header diagnostic: %s\n", marcus_error(probe));
        CHECK(std::strstr(marcus_error(probe), "record header") != nullptr);
        marcus_destroy(probe);
        CHECK(filename.FDelete());
    }
}
void Fixture(const char *path)
{
    String name;
    name.SetSzs(const_cast<char *>(path));
    Filename filename;
    CHECK(filename.FBuildFromPath(&name));
    auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    CHECK(file);
    ChunkNumber root;
    CHECK(MovieData::FCreateRoot(file, &root));
    auto wave = Sound(file, false), midi = Sound(file, true);
    auto title = Caption(file, false), credits = Caption(file, true);
    for (int index = 0; index < 5; ++index)
    {
        SceneData scene;
        scene.firstFrame = -2;
        scene.lastFrame = 4;
        scene.transition = static_cast<TRANS>(index + 1 < transLim ? index + 1 : transCut);
        ChunkNumber number;
        CHECK(scene.FCreate(file, &number));
        CHECK(file->FAdoptChild(kctgMvie, root, kctgScen, number, index));
        CHECK(file->FAdoptChild(kctgScen, number, kctgMsnd, wave, 7));
        CHECK(file->FAdoptChild(kctgScen, number, kctgMsnd, midi, 8));
        auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
        auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
        CHECK(starts && frames);
        if (index == 4)
        {
            Actor(file, number, wave, starts);
            SceneEvent background = {-2, sevtSetBkgd};
            TAGOnFile tag = {2, 0, kctgBkgd, 0};
            CHECK(starts->FInsert(starts->IvMac(), sizeof(tag), &tag, &background));
        }
        for (unsigned slot = 0; slot < 2; ++slot)
        {
            SceneEvent event = {-2, sevtAddTbox};
            CHECK(file->FAdoptChild(kctgScen, number, kctgTbox, slot ? credits : title, slot));
            CHECK(starts->FInsert(starts->IvMac(), sizeof(slot), &slot, &event));
        }
        SceneEvent event = {-1, sevtPause};
        SceneEventPause pause = {witUntilClick, 0};
        CHECK(frames->FInsert(frames->IvMac(), sizeof(pause), &pause, &event));
        SceneSound(frames, 0, midi, 8, true, true);
        if (index == 4)
        {
            SceneEvent change = {1, sevtChngCamera};
            std::uint32_t camera = 1;
            CHECK(frames->FInsert(frames->IvMac(), sizeof(camera), &camera, &change));
        }
        event.nfrm = 1;
        pause = {witForTime, 400};
        CHECK(frames->FInsert(frames->IvMac(), sizeof(pause), &pause, &event));
        SceneSound(frames, 2, wave, 7, false, false, true);
        event.nfrm = 2;
        pause = {witUntilSnd, 0};
        CHECK(frames->FInsert(frames->IvMac(), sizeof(pause), &pause, &event));
        CHECK(SceneData::FWriteEvents(file, number, frames, starts));
        ReleasePpo(&starts);
        ReleasePpo(&frames);
    }
    CHECK(file->FSave(kctgSoc));
    ReleasePpo(&file);
}
MarcusSoundTiming FixtureTiming(const char *path, const char *resources)
{
    ThreeMM::Archive archive;
    ThreeMM::Preview preview;
    ThreeMM::Error error;
    CHECK(archive.Open(path, error) && preview.Open(path, resources, error));
    for (const auto &chunk : archive.Chunks())
        if (chunk.id.type == kctgScen)
        {
            CHECK(preview.Select(chunk.id, error));
            ThreeMM::PreviewPresentation presentation;
            MarcusSoundTiming timing;
            CHECK(preview.ReadPresentation(presentation, error));
            CHECK(MarcusPlayback::ReadTiming(presentation, timing, error));
            CHECK(timing.scrolling.at(0) > 0);
            return timing;
        }
    CHECK(false);
    return {};
}
void CarryIntegration(const char *path, const char *resources, const char *content)
{
    String name;
    name.SetSzs(const_cast<char *>(path));
    Filename filename;
    CHECK(filename.FBuildFromPath(&name));
    auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    CHECK(file);
    ChunkNumber root;
    CHECK(MovieData::FCreateRoot(file, &root));
    const auto longSound = Sound(file, false, 96000), otherSound = Sound(file, false);
    const int first[] = {-2, 100, -10, 0}, last[] = {0, 105, -5, 5};
    for (int index = 0; index < 4; ++index)
    {
        SceneData scene;
        scene.firstFrame = first[index]; scene.lastFrame = last[index]; scene.transition = transCut;
        ChunkNumber number;
        CHECK(scene.FCreate(file, &number) && file->FAdoptChild(kctgMvie, root, kctgScen, number, index));
        CHECK(file->FAdoptChild(kctgScen, number, kctgMsnd, index ? otherSound : longSound, 7));
        auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
        auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
        CHECK(starts && frames);
        if (!index)
            SceneSound(frames, 0, longSound + 123, 7, false);
        else if (index == 1)
            SceneSound(frames, first[index], longSound + 123, 7, false, false, false, stySpeech);
        CHECK(SceneData::FWriteEvents(file, number, frames, starts));
        ReleasePpo(&starts); ReleasePpo(&frames);
    }
    CHECK(file->FSave(kctgSoc));
    ReleasePpo(&file);
    player = marcus_create();
    CHECK(player && marcus_open(player, path, resources, content));
    BackgroundTiming();
    CHECK(marcus_select_scene(player, 2)); // Direct jump must reconstruct earlier tails.
    MarcusInfo info;
    MarcusSound sound;
    MarcusWaveformInfo wave;
    MarcusWaveformColumn columns[4];
    CHECK(marcus_info(player, &info) && info.sound_count == 1 && info.frame == -10);
    CHECK(marcus_sound(player, 0, &sound) && sound.carried && sound.origin_scene == 0 && sound.origin_frame == 3);
    CHECK(std::abs(sound.timeline_frame + 17) < 1e-6 && std::abs(sound.end_frame + 5) < 1e-6);
    CHECK(marcus_sound_waveform(player, 0, 7.0 / 6, .01, 4, columns, &wave));
    CHECK(wave.kind == MARCUS_WAVEFORM_SAMPLED && std::abs(wave.seconds - 2) < 1e-6);
    CHECK(columns[0].minimum < -.2f && columns[0].maximum > .2f);
    CHECK(marcus_sound_waveform_frames(player, 0, -10, .06, 4, columns, &wave));
    CHECK(columns[0].minimum < -.2f && columns[0].maximum > .2f);
    CHECK(marcus_sound_waveform_frames(player, 0, -4, .06, 4, columns, &wave));
    CHECK(columns[0].minimum == 0 && columns[0].maximum == 0);
    CHECK(!marcus_sound_waveform_frames(player, 0, -10, 0, 4, columns, &wave));
    CHECK(marcus_select_scene(player, 3) && marcus_info(player, &info) && info.sound_count == 0);
    CHECK(marcus_select_scene(player, 1) && marcus_info(player, &info) && info.sound_count == 2);
    // Both cues use the same stored ID and child slot, but resolve in different scenes.
    CHECK(marcus_sound_waveform(player, 1, 0, .01, 4, columns, &wave) && std::abs(wave.seconds - .1) < 1e-6);
    CHECK(marcus_sound_waveform(player, 0, 0, .01, 4, columns, &wave) && std::abs(wave.seconds - 2) < 1e-6);
    MarcusAudioStats stats;
    CHECK(marcus_audio_stats(player, &stats) && !stats.wave_sounds && !stats.midi_sounds);
    CHECK(marcus_info(player, &info) && info.frame == 100);
    CHECK(marcus_seek(player, 103));
    MarcusPlaybackState state;
    CHECK(marcus_update(player, 0, 0, 0, 0, &state));
    CHECK(marcus_audio_stats(player, &stats) && !stats.wave_sounds); // Scrubbing stays silent.
    CHECK(marcus_update(player, 1, 0, 0, 0, &state));
    CHECK(marcus_audio_stats(player, &stats) && stats.wave_sounds == 1); // Restore the earlier scene's effect.
    CHECK(marcus_update(player, 0, 0, 0, 0, &state));
    CHECK(marcus_update(player, 1, 0, 0, 0, &state));
    CHECK(marcus_audio_stats(player, &stats) && stats.wave_sounds == 1); // Pause/resume preserves the voice.
    CHECK(marcus_select_scene(player, 2) && marcus_seek(player, -6));
    const auto audibleBefore = stats.audible_frames;
    CHECK(marcus_update(player, 1, 0, 0, 0, &state));
    SDL_Delay(350);
    CHECK(marcus_audio_stats(player, &stats) && stats.audible_frames > audibleBefore && stats.wave_sounds == 2);
    const auto completed = stats.audible_frames;
    SDL_Delay(200);
    CHECK(marcus_audio_stats(player, &stats) && stats.audible_frames == completed); // Correct offset leaves only 1/6 s.
    CHECK(marcus_select_scene(player, 0) && marcus_seek(player, 0));
    CHECK(marcus_update(player, 1, 0, 0, 0, &state));
    CHECK(marcus_update(player, 1, 0, 170, 0, &state) && state.scene_changed);
    CHECK(marcus_info(player, &info) && info.scene == 1 && info.sound_count == 2);
    CHECK(marcus_sound(player, 0, &sound) && sound.carried && sound.origin_scene == 0);
    marcus_destroy(player);
    player = nullptr;
}
MarcusPlaybackState Tick(double elapsed, bool playing = true, bool click = false, bool loop = false)
{
    MarcusPlaybackState state;
    CHECK(marcus_update(player, playing, loop, elapsed, click, &state));
    return state;
}
MarcusInfo Info()
{
    MarcusInfo info;
    CHECK(marcus_info(player, &info));
    return info;
}
MarcusMovieTime MovieTime()
{
    MarcusMovieTime time;
    CHECK(marcus_movie_time(player, &time));
    CHECK(time.position_seconds >= 0 && time.position_seconds <= time.duration_seconds);
    return time;
}
std::vector<byte> Image(int authored = 1, int width = 480, int height = 360)
{
    MarcusImage image;
    CHECK(marcus_render(player, width, height, authored, 0, 0, 1, &image));
    return {image.rgba, image.rgba + std::size_t(image.pitch) * image.height};
}
std::size_t Visible(const std::vector<byte> &pixels)
{
    std::size_t count = 0;
    for (std::size_t i = 3; i < pixels.size(); i += 4)
        count += pixels[i] != 0;
    return count;
}
} // namespace
int main(int argc, char **argv)
{
    RetailScrollSteps();
    if (argc == 5 && std::strcmp(argv[1], "--sample") == 0)
    {
        player = marcus_create();
        CHECK(player && marcus_open(player, argv[2], argv[3], argv[4]));
        const int scenes = Info().scene_count;
        for (int scene = 0; scene < scenes; ++scene)
        {
            CHECK(marcus_select_scene(player, scene));
            const auto first = Info().first_frame, last = Info().last_frame;
            for (long frame = first; frame <= last && frame - first < 5; ++frame)
            {
                CHECK(marcus_seek(player, frame));
                Tick(0);
                Image();
            }
        }
        MarcusAudioStats stats;
        CHECK(marcus_audio_stats(player, &stats));
        std::printf("Sampled %d scenes: WAVE voices=%lu, MIDI voices=%lu\n", scenes, stats.wave_sounds,
                    stats.midi_sounds);
        marcus_destroy(player);
        return 0;
    }
    if (argc != 4)
        return 2;
    WaveformPeaks();
    FailedTiming();
    CarryScheduling();
    PresentationClock();
    InvalidMovieHeaders(argv[1], argv[2], argv[3]);
    Fixture(argv[1]);
    const auto authoredTiming = FixtureTiming(argv[1], argv[2]);
    const auto movieBefore = Bytes(argv[1]), resourcesBefore = Bytes(argv[2]);
    player = marcus_create();
    CHECK(player && marcus_open(player, argv[1], argv[2], argv[3]));
    // Reopening cancels and joins a pending job before releasing its leases.
    MarcusMovieTime pending;
    CHECK(marcus_movie_time_async(player, &pending) == 0);
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]));
    BackgroundTiming();
    CHECK(Info().first_frame == -2 && Info().object_count == 0);
    CHECK(!marcus_movie_time(nullptr, nullptr) && !marcus_movie_time(player, nullptr));
    const double sceneSeconds = 7.0 / 6 + authoredTiming.scrolling.at(0) + .4 + .2;
    const double movieSeconds = 5 * sceneSeconds + 12 + 1.0 / 6;
    CHECK(MovieTime().position_seconds == 0);
    CHECK(std::abs(MovieTime().duration_seconds - movieSeconds) < 1e-6);
    // Read-only dope sheet metadata must neither move the playhead nor start
    // audio. Queued entries retain their shared authored start frame.
    CHECK(Info().sound_count == 3 && Info().sound_track_count == 2);
    MarcusSound cue, queued;
    MarcusSoundTrack soundTrack;
    CHECK(!marcus_sound(nullptr, 0, &cue) && !marcus_sound(player, -1, &cue));
    CHECK(!marcus_sound(player, 3, &cue) && !marcus_sound(player, 0, nullptr));
    CHECK(!marcus_sound_track(player, -1, &soundTrack) && !marcus_sound_track(player, 2, &soundTrack));
    CHECK(!marcus_sound_track(nullptr, 0, &soundTrack) && !marcus_sound_track(player, 0, nullptr));
    CHECK(marcus_sound(player, 0, &cue));
    CHECK(cue.frame == 0 && cue.style == MARCUS_SOUND_MUSIC && cue.loop && !cue.actor_sound);
    CHECK(cue.source_id == 0 && cue.child == 8 && cue.volume == 0x10000);
    CHECK(marcus_sound(player, 1, &cue) && marcus_sound(player, 2, &queued));
    CHECK(cue.frame == 2 && queued.frame == 2 && !cue.queue && queued.queue && cue.track == queued.track);
    CHECK(marcus_sound_track(player, cue.track, &soundTrack) && soundTrack.cue_count == 2);
    CHECK(!std::strcmp(soundTrack.name, "Scene / Effects"));
    CHECK(Info().frame == -2);
    MarcusAudioStats sheetStats;
    CHECK(marcus_audio_stats(player, &sheetStats) && !sheetStats.wave_sounds && !sheetStats.midi_sounds);
    MarcusWaveformInfo waveform;
    MarcusWaveformColumn peaks[16], repeated[16];
    CHECK(marcus_sound_waveform(player, 0, 0, .01, 16, peaks, &waveform));
    CHECK(waveform.kind == MARCUS_WAVEFORM_MIDI && waveform.message[0]);
    CHECK(marcus_sound_waveform(player, 1, 0, .01, 16, peaks, &waveform));
    CHECK(waveform.kind == MARCUS_WAVEFORM_SAMPLED && std::abs(waveform.seconds - .1) < 1e-6);
    CHECK(peaks[0].minimum < -.2f && peaks[0].maximum > .2f);
    CHECK(peaks[15].minimum == 0 && peaks[15].maximum == 0);
    CHECK(marcus_sound_waveform(player, 2, 0, .01, 16, repeated, &waveform));
    CHECK(!std::memcmp(peaks, repeated, sizeof(peaks))); // Shared queued sample.
    CHECK(marcus_sound_waveform(player, 1, .2, .01, 16, repeated, &waveform));
    for (const auto &peak : repeated)
        CHECK(peak.minimum == 0 && peak.maximum == 0);
    CHECK(!marcus_sound_waveform(nullptr, 1, 0, .01, 16, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, -1, 0, .01, 16, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, 3, 0, .01, 16, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, 1, -1, .01, 16, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, 1, 0, 0, 16, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, 1, std::numeric_limits<double>::infinity(), .01, 16, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, 1, 0, std::numeric_limits<double>::quiet_NaN(), 16, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, 1, 0, .01, 0, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, 1, 0, .01, 4097, peaks, &waveform));
    CHECK(!marcus_sound_waveform(player, 1, 0, .01, 16, nullptr, &waveform));
    CHECK(!marcus_sound_waveform(player, 1, 0, .01, 16, peaks, nullptr));
    CHECK(Info().frame == -2);
    CHECK(marcus_audio_stats(player, &sheetStats) && !sheetStats.wave_sounds && !sheetStats.midi_sounds);
    const auto title = Image();
    CHECK(std::count(title.begin(), title.end(), byte(255)) > 1000);
    const auto orbitTitle = Image(0);
    CHECK(title != orbitTitle && Visible(orbitTitle) > 100);
    MarcusImage inspection;
    CHECK(marcus_render(player, 480, 360, 0, 0, 0, 1, &inspection));
    CHECK(inspection.scene_camera.valid && inspection.scene_camera.display_depth == 10);
    auto camera = inspection.camera;
    CHECK(marcus_render_fly(player, 480, 360, &camera, &inspection));
    auto flyTitle = std::vector<byte>(inspection.rgba, inspection.rgba + 480 * 360 * 4);
    CHECK(flyTitle == orbitTitle);
    camera.x += 5;
    CHECK(marcus_render_fly(player, 480, 360, &camera, &inspection));
    flyTitle.assign(inspection.rgba, inspection.rgba + 480 * 360 * 4);
    CHECK(flyTitle != orbitTitle && Visible(flyTitle) > 100);
    camera.yaw += 180;
    CHECK(marcus_render_fly(player, 480, 360, &camera, &inspection));
    flyTitle.assign(inspection.rgba, inspection.rgba + 480 * 360 * 4);
    CHECK(Visible(flyTitle) == 0); // The authored plane is behind the flight camera.
    CHECK(marcus_seek(player, -1) && Visible(Image(0)) == 0);
    CHECK(marcus_seek(player, -2) && Image(0) == orbitTitle);
    CHECK(marcus_seek(player, 0));
    const auto credits = Image();
    CHECK(credits != title);
    CHECK(marcus_seek(player, -2) && Image() == title);
    Tick(0);
    Tick(170);
    CHECK(Info().frame == -1);
    CHECK(Tick(0).wait == 1);
    const auto clickTime = MovieTime().position_seconds;
    Tick(10000);
    CHECK(Info().frame == -1);
    CHECK(MovieTime().position_seconds == clickTime);
    Tick(0, false);
    CHECK(Tick(10000).wait == 1); // Resume does not consume a click pause.
    CHECK(Tick(0, true, true).wait == 0);
    Tick(170);
    CHECK(Info().frame == 0);
    CHECK(Tick(0).scrolling);
    const auto scrollTime = MovieTime().position_seconds;
    Tick(100);
    CHECK(std::abs(MovieTime().position_seconds - scrollTime - .1) < 1e-6);
    double scrollElapsed = 100;
    CHECK(Info().frame == 0 && Image() != credits);
    const auto firstScrollStep = Image();
    Tick(0, false);
    Tick(10000); // Resume discards paused time without moving credits.
    CHECK(Image() == firstScrollStep && Info().frame == 0);
    MarcusAudioStats stats;
    CHECK(marcus_audio_stats(player, &stats) && stats.midi_sounds == 1);
    SDL_Delay(150);
    CHECK(marcus_audio_stats(player, &stats) && stats.audible_frames > 0);
    for (int i = 0; i < 200 && Info().frame == 0; ++i)
    {
        Tick(100);
        scrollElapsed += 100;
    }
    CHECK(Info().frame == 1);
    // Each deliberately late 100 ms callback advances just one pixel, plus
    // the completion-check alarm and ordinary authored-frame delay.
    const auto scrollAlarms = std::lround(authoredTiming.scrolling.at(0) * 12);
    CHECK(scrollElapsed >= scrollAlarms * 100 + 100);
    CHECK(scrollElapsed <= scrollAlarms * 100 + 200);
    CHECK(Tick(0).wait == 3);
    Tick(200);
    Tick(0, false);
    const auto pausedTime = MovieTime().position_seconds;
    Tick(10000); // User pause freezes the remaining authored duration.
    CHECK(MovieTime().position_seconds == pausedTime);
    CHECK(Info().frame == 1);
    CHECK(Tick(199).wait == 3);
    CHECK(Tick(1).wait == 0);
    Tick(170);
    CHECK(Info().frame == 2 && Tick(0).wait == 2);
    CHECK(marcus_audio_stats(player, &stats) && stats.wave_sounds == 2);
    Tick(10000);
    CHECK(Info().frame == 2); // Sound completion follows rendered audio, not host dt.
    SDL_Delay(500);
    CHECK(Tick(0).wait == 0); // Looped music does not block a sound pause.
    Tick(170);
    CHECK(Info().frame == 3);
    CHECK(marcus_seek(player, -2) && Image() == title);
    for (int scene = 0; scene < 4; ++scene)
    {
        CHECK(marcus_select_scene(player, scene) && marcus_seek(player, 4));
        const auto outgoing = Image();
        Tick(0);
        CHECK(marcus_audio_stats(player, &stats));
        const auto musicBeforeTransition = stats.midi_sounds;
        // A work-camera render between the movie render and the cut must not
        // replace the outgoing movie image used by the transition compositor.
        CHECK(marcus_render(player, 480, 360, 0, 35, 15, 1, &inspection));
        auto workCamera = inspection.camera;
        workCamera.x += workCamera.move_speed;
        CHECK(marcus_render_fly(player, 480, 360, &workCamera, &inspection));
        const auto beforeTransition = MovieTime().position_seconds;
        auto state = Tick(170);
        CHECK(state.scene_changed && state.transition && Info().scene == scene + 1);
        CHECK(std::abs(MovieTime().position_seconds - beforeTransition - 1.0 / 6) < 1e-6);
        CHECK(marcus_audio_stats(player, &stats) && stats.midi_sounds == musicBeforeTransition);
        const auto initial = Image();
        if (scene < 3)
            CHECK(initial == outgoing);
        Tick(scene == 3 ? 80 : 1000);
        CHECK(std::abs(MovieTime().position_seconds - beforeTransition - 1.0 / 6 -
                       (scene == 3 ? .08 : 1)) < 1e-6);
        const auto middle = Image();
        if (scene < 3)
            CHECK(middle != initial);
        Tick(5000);
        CHECK(!Tick(0).transition && (scene == 3 || Image() == title));
        CHECK(marcus_audio_stats(player, &stats) && stats.midi_sounds == musicBeforeTransition);
        Tick(170);
        CHECK(Info().frame == -1 && Tick(0).wait == 1);
        Tick(0, true, true);
        Tick(170);
        CHECK(Info().frame == 0);
        CHECK(marcus_audio_stats(player, &stats) && stats.midi_sounds == musicBeforeTransition);
    }
    CHECK(marcus_audio_stats(player, &stats));
    const auto soundsBefore = stats.wave_sounds;
    CHECK(marcus_select_scene(player, 4));
    CHECK(Info().sound_count == 6 && Info().sound_track_count == 3);
    CHECK(marcus_sound(player, 0, &cue) && cue.carried && cue.origin_scene == 0 && cue.loop);
    CHECK(marcus_sound(player, 1, &cue) && cue.actor_sound && cue.actor_id == 42 && cue.frame == -2);
    CHECK(cue.motion && cue.motion_cel == 0 && cue.child == 7);
    CHECK(marcus_sound(player, 2, &queued) && queued.actor_sound && !queued.motion && queued.motion_cel == -1);
    CHECK(cue.track == queued.track && marcus_sound_track(player, cue.track, &soundTrack));
    CHECK(!std::strcmp(soundTrack.name, "Actor 42 / Effects") && soundTrack.cue_count == 2);
    CHECK(Info().frame == -2 && marcus_audio_stats(player, &sheetStats) && sheetStats.wave_sounds == soundsBefore);
    CHECK(marcus_sound_waveform(player, 1, 0, .01, 16, repeated, &waveform));
    CHECK(waveform.kind == MARCUS_WAVEFORM_SAMPLED && !std::memcmp(peaks, repeated, sizeof(peaks)));
    Tick(0);
    CHECK(marcus_audio_stats(player, &stats) && stats.wave_sounds == soundsBefore + 1);
    Tick(170);
    CHECK(Info().frame == -1 && marcus_audio_stats(player, &stats) && stats.wave_sounds == soundsBefore + 2);
    CHECK(Tick(0).wait == 1);
    CHECK(marcus_seek(player, -2));
    Tick(0);
    CHECK(marcus_audio_stats(player, &stats) && stats.wave_sounds == soundsBefore + 3);
    CHECK(marcus_seek(player, 0));
    Tick(0); // Keep looping MIDI active throughout the mute check.
    marcus_set_muted(player, 1);
    SDL_Delay(100);
    CHECK(marcus_audio_stats(player, &stats));
    const auto audibleBefore = stats.audible_frames;
    SDL_Delay(100);
    CHECK(marcus_audio_stats(player, &stats) && stats.audible_frames == audibleBefore);
    marcus_set_muted(player, 0);
    SDL_Delay(100);
    CHECK(marcus_audio_stats(player, &stats) && stats.audible_frames > audibleBefore);
    CHECK(marcus_seek(player, -2));
    CHECK(marcus_render(player, 480, 360, 0, 0, 0, 1, &inspection));
    const auto firstCamera = inspection.scene_camera;
    const auto orbitProjection = inspection.projection;
    CHECK(firstCamera.valid && firstCamera.display_depth == 10);
    CHECK(marcus_seek(player, 1));
    CHECK(marcus_render(player, 480, 360, 0, 0, 0, 1, &inspection));
    CHECK(std::memcmp(firstCamera.transform, inspection.scene_camera.transform, sizeof(firstCamera.transform)));
    CHECK(!std::memcmp(orbitProjection.transform, inspection.projection.transform, sizeof(orbitProjection.transform)));
    CHECK(marcus_select_scene(player, 4) && marcus_seek(player, 4));
    Tick(0);
    CHECK(!Tick(170).playing && Info().scene == 4 && Info().frame == 4);
    CHECK(MovieTime().position_seconds == MovieTime().duration_seconds);
    Tick(0, true, false, true);
    auto wrap = Tick(170, true, false, true);
    CHECK(wrap.playing && wrap.changed && !wrap.scene_changed && !wrap.transition && Info().scene == 4 &&
          Info().frame == -2);
    CHECK(std::abs(MovieTime().position_seconds - (4 * sceneSeconds + 12 + 1.0 / 6)) < 1e-6);
    CHECK(std::abs(MovieTime().duration_seconds - movieSeconds) < 1e-6);
    // The loop applies to any current scene, including ones with successors.
    // Rewinding must preserve inspection framing and restart first-frame audio.
    for (int scene : {0, 2, 4})
    {
        CHECK(marcus_select_scene(player, scene));
        const auto firstShot = Image();
        CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &inspection));
        const auto workProjection = inspection.projection;
        auto fly = inspection.camera;
        fly.x += fly.move_speed;
        for (int lap = 0; lap < 2; ++lap)
        {
            CHECK(marcus_seek(player, 4));
            Tick(0, true, false, true);
            wrap = Tick(170, true, false, true);
            CHECK(wrap.playing && wrap.changed && !wrap.scene_changed && !wrap.transition);
            CHECK(Info().scene == scene && Info().frame == Info().first_frame && Image() == firstShot);
            CHECK(marcus_render(player, 480, 360, 0, 25, 12, 1, &inspection));
            CHECK(!std::memcmp(workProjection.transform, inspection.projection.transform,
                               sizeof(workProjection.transform)));
            CHECK(marcus_render_fly(player, 480, 360, &fly, &inspection));
            CHECK(std::abs(inspection.camera.x - fly.x) < .001 && std::abs(inspection.camera.y - fly.y) < .001);
            CHECK(marcus_audio_stats(player, &stats));
            const auto voices = stats.wave_sounds;
            Tick(0, true, false, true);
            CHECK(marcus_audio_stats(player, &stats) && stats.wave_sounds == voices + (scene == 4 ? 1 : 0));
            Tick(0, true, false, true);
            CHECK(marcus_audio_stats(player, &stats) && stats.wave_sounds == voices + (scene == 4 ? 1 : 0));
        }
        Tick(170, true, false, true);
        CHECK(Info().frame == -1 && Tick(0, true, false, true).wait == 1);
        CHECK(Tick(5000, true, false, true).wait == 1 && Info().frame == -1);
        CHECK(!Tick(0, true, true, true).wait);
    }
    // Turning Loop off resumes normal scene progression and transitions.
    CHECK(marcus_select_scene(player, 2) && marcus_seek(player, 4));
    Image();
    Tick(0);
    CHECK(Tick(170).scene_changed && Info().scene == 3);
    // The movie-wide scrubber resolves presentation time across chapter cuts,
    // transitions and held frames without starting any audio voices.
    MarcusAudioStats beforeSeek, afterSeek;
    CHECK(marcus_audio_stats(player, &beforeSeek));
    MarcusSceneTime chapter;
    CHECK(!marcus_scene_time(player, -1, &chapter) && !marcus_scene_time(player, 5, &chapter));
    CHECK(!marcus_scene_time(player, 0, nullptr));
    double previousEnd = 0;
    for (int scene = 0; scene < 5; ++scene)
    {
        CHECK(marcus_scene_time(player, scene, &chapter));
        CHECK(std::abs(chapter.start_seconds - previousEnd) < 1e-6);
        CHECK(chapter.end_seconds > chapter.start_seconds);
        CHECK(marcus_seek_time(player, chapter.start_seconds));
        CHECK(Info().scene == scene && Info().frame == Info().first_frame);
        CHECK(marcus_seek_time(player, chapter.end_seconds - .01));
        CHECK(Info().scene == scene && Info().frame == Info().last_frame);
        previousEnd = chapter.end_seconds;
    }
    CHECK(std::abs(previousEnd - movieSeconds) < 1e-6);
    CHECK(marcus_seek_time(player, -100) && Info().scene == 0 && Info().frame == -2);
    CHECK(MovieTime().position_seconds == 0);
    CHECK(marcus_seek_time(player, 1.0 / 3 + authoredTiming.scrolling.at(0) / 2));
    CHECK(Info().scene == 0 && Info().frame == 0); // Mid-scroll seeks to its displayed frame.
    CHECK(marcus_seek_time(player, movieSeconds + 100));
    CHECK(Info().scene == 4 && Info().frame == 4);
    CHECK(MovieTime().position_seconds == MovieTime().duration_seconds);
    CHECK(std::abs(MovieTime().position_seconds - movieSeconds) < 1e-6);
    CHECK(!marcus_seek_time(player, std::numeric_limits<double>::infinity()));
    CHECK(!marcus_seek_time(player, std::numeric_limits<double>::quiet_NaN()));
    CHECK(Info().scene == 4 && Info().frame == 4);
    CHECK(marcus_audio_stats(player, &afterSeek) && afterSeek.wave_sounds == beforeSeek.wave_sounds &&
          afterSeek.midi_sounds == beforeSeek.midi_sounds);
    CHECK(!marcus_update(player, 1, 0, -1, 0, nullptr));
    marcus_destroy(player);
    player = nullptr;
    CHECK(Bytes(argv[1]) == movieBefore && Bytes(argv[2]) == resourcesBefore);
    CarryIntegration((std::string(argv[1]) + ".carry.3mm").c_str(), argv[2], argv[3]);
    std::puts(
        "Headless playback: caption visibility/rewind, Unicode, scrolling, click/time/sound waits, MIDI/WAVE audio,");
    std::puts("queued clips, outgoing scene transitions, looping, pause/resume, and unchanged input files.");
    return 0;
}
