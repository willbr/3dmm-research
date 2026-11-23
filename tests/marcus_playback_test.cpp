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
#include "storyboard.h"
#include "timing_job.h"
#include "retail_scroll.h"
#include "mm/pixel_raster.h"
#include "mm/playback.h"
#include <miniz.h>
#include "soc_core.h"
#include "actor_track_fixture.h"
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

    MM::Preview unopened;
    MM::PreviewSoundCue cue;
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
    auto read = [](const MM::PreviewSoundCue &cue) {
        MarcusWaveformInfo audio = {};
        audio.kind = cue.silent ? MARCUS_WAVEFORM_SILENT : cue.style == MARCUS_SOUND_MUSIC
                     ? MARCUS_WAVEFORM_MIDI : MARCUS_WAVEFORM_SAMPLED;
        audio.seconds = cue.sound.number == 4 ? .25 : cue.sound.number == 5 ? 1 : 2;
        audio.resolved_chunk = cue.sound.number;
        return audio;
    };
    MM::PreviewSoundCue music, effect, speech, loop;
    music.frame = 10; music.style = MARCUS_SOUND_MUSIC; music.loop = true; music.sound.number = 100;
    effect.frame = 13; effect.style = MARCUS_SOUND_EFFECT; effect.sound.number = 1;
    speech.frame = 14; speech.style = MARCUS_SOUND_SPEECH; speech.actorSound = true;
    speech.actor = 7; speech.sound.number = 2;
    loop = effect; loop.frame = 10; loop.loop = true; loop.actorSound = true; loop.actor = 9;
    timeline.Append(first, {music, loop, effect, speech}, read);
    music.frame = -3;
    timeline.Append(later, {music}, read);
    CHECK(timeline.Scene(1).SoundCount() == 4); // Three tails plus the duplicate music marker.
    CHECK(timeline.Scene(1).Sound(0).scene == 0 && timeline.Scene(1).Sound(0).start == 0);
    CHECK(std::abs(timeline.Scene(1).start - timeline.Scene(1).Sound(1).start - .5) < 1e-6);
    CHECK(timeline.Scene(0).Sound(1).end == 1); // Sampled loops stop at the cut.
    effect.frame = -5; effect.sound.number = 4;
    speech.frame = -5; speech.sound.number = 5; speech.queue = true;
    timeline.Append(later, {effect, speech}, read);
    CHECK(timeline.Scene(2).SoundCount() == 4); // Music, speech tail, replacement, queued speech.
    CHECK(timeline.Scene(0).Sound(2).end == 2); // Replacement truncates the carried effect.
    CHECK(std::abs(timeline.Scene(2).Sound(3).start - 8.0 / 3) < 1e-6);
    const auto queuedResume = timeline.Resume(2, -3);
    CHECK(queuedResume.size() == 3 && queuedResume.back().cue.queue && queuedResume.back().seconds == 0);
    timeline.Append(later, {}, read);
    CHECK(timeline.Scene(3).SoundCount() == 2); // Only music and the queued speech remain.
    music.frame = -5; music.silent = true;
    timeline.Append(later, {music}, read);
    CHECK(timeline.Scene(4).SoundCount() == 1 && timeline.Scene(4).Sound(0).cue.silent);
    std::ostringstream encoded(std::ios::binary);
    CHECK(timeline.Write(encoded));
    MarcusSoundTimeline imported;
    const std::vector<MarcusScene> expected = {first, later, later, later, later};
    std::istringstream complete(encoded.str(), std::ios::binary);
    CHECK(imported.Read(complete, expected) && imported.Size() == timeline.Size());
    for (std::size_t i = 0; i < imported.Size(); ++i)
    {
        CHECK(imported.Scene(i).start == timeline.Scene(i).start);
        CHECK(imported.Scene(i).SoundCount() == timeline.Scene(i).SoundCount());
        for (std::size_t j = 0; j < imported.Scene(i).SoundCount(); ++j)
        {
            CHECK(imported.Scene(i).Sound(j).start == timeline.Scene(i).Sound(j).start);
            CHECK(imported.Scene(i).Sound(j).end == timeline.Scene(i).Sound(j).end);
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
    CHECK(timeline.Scene(1).SoundCount() == 1 && timeline.Scene(1).Sound(0).cue.queue);
    CHECK(timeline.Scene(1).Sound(0).start == 1 && timeline.Scene(1).Sound(0).end == 2);
}
void RangeScheduling()
{
    std::vector<MarcusScene> ranges(2);
    ranges[0].chunk = 10; ranges[0].first_frame = 10; ranges[0].last_frame = 15;
    ranges[1].chunk = 20; ranges[1].first_frame = -5; ranges[1].last_frame = 0;
    MM::PreviewSoundCue loop, queued, speech, late;
    loop.frame = 10; loop.loop = true; loop.style = MARCUS_SOUND_EFFECT; loop.sound.number = 1;
    queued = loop; queued.frame = 12; queued.loop = false; queued.queue = true; queued.sound.number = 2;
    speech.frame = 14; speech.style = MARCUS_SOUND_SPEECH; speech.sound.number = 3;
    late = speech; late.frame = 17; late.sound.number = 4;
    const std::vector<std::vector<MM::PreviewSoundCue>> cues = {{loop, queued, speech, late}, {}};
    const MMMovieTimingScroll scrolls[] = {{11, .5}, {17, .25}};
    const MMMovieTimingPause firstPauses[] = {{12, 2, 0}, {18, 3, 400}}, secondPauses[] = {{-4, 2, 0}};
    const MMMovieTimingHolds timing[] = {{.75, 2, 2, scrolls, firstPauses}, {0, 0, 1, nullptr, secondPauses}};
    int reads = 0;
    auto read = [&](const MM::PreviewSoundCue &cue) {
        ++reads;
        MarcusWaveformInfo audio = {};
        audio.kind = MARCUS_WAVEFORM_SAMPLED;
        audio.seconds = cue.sound.number == 2 ? .75 : 2;
        audio.resolved_chunk = cue.sound.number;
        return audio;
    };
    auto build = [&](const std::vector<MarcusScene> &info) {
        MarcusSoundTimeline result;
        result.SetRate(8);
        for (std::size_t i = 0; i < info.size(); ++i) result.Append(info[i], cues[i], read, timing[i]);
        return result;
    };
    auto original = build(ranges);
    std::ostringstream encoded(std::ios::binary);
    CHECK(original.Write(encoded));
    MarcusSoundTimeline cached;
    std::istringstream input(encoded.str(), std::ios::binary);
    CHECK(cached.Read(input, ranges));
    std::istringstream trailing(encoded.str() + "x", std::ios::binary);
    CHECK(!cached.Read(trailing, ranges) && cached.Size() == ranges.size() &&
          cached.Scene(0).range.chunk == ranges[0].chunk);
    for (int edit = 0; edit < 3; ++edit)
    {
        auto changed = ranges;
        if (edit != 2)
        {
            changed[0].first_frame -= edit + 1;
            changed[0].last_frame += 4;
            changed[1].first_frame -= 2;
            changed[1].last_frame += 3;
        }
        const auto priorReads = reads;
        CHECK(cached.Reframe(changed) && reads == priorReads);
        const auto fresh = build(changed);
        for (std::size_t i = 0; i < changed.size(); ++i)
        {
            const auto &actual = cached.Scene(i), &expected = fresh.Scene(i);
            CHECK(actual.start == expected.start && actual.SoundCount() == expected.SoundCount());
            for (double frame = changed[i].first_frame; frame <= double(changed[i].last_frame) + 1; frame += .5)
            {
                const double time = actual.TimeAt(frame);
                CHECK(time == expected.TimeAt(frame) && actual.FrameAt(time) == expected.FrameAt(time));
            }
            for (std::size_t cue = 0; cue < actual.SoundCount(); ++cue)
            {
                CHECK(actual.Sound(cue).start == expected.Sound(cue).start &&
                      actual.Sound(cue).end == expected.Sound(cue).end &&
                      actual.Sound(cue).cue.frame == expected.Sound(cue).cue.frame);
            }
            const auto resume = cached.Resume(i, changed[i].first_frame), expectedResume = fresh.Resume(i, changed[i].first_frame);
            CHECK(resume.size() == expectedResume.size());
            for (std::size_t cue = 0; cue < resume.size(); ++cue)
                CHECK(resume[cue].seconds == expectedResume[cue].seconds && resume[cue].cue.queue == expectedResume[cue].cue.queue);
        }
    }
    auto invalid = ranges;
    invalid[0].chunk = 999;
    CHECK(!cached.Reframe(invalid) && cached.Scene(0).range.chunk == ranges[0].chunk);
}
void PresentationClock()
{
    MarcusScene first = {}, later = {};
    first.first_frame = 10; first.last_frame = 15;
    later.first_frame = -5; later.last_frame = 0;
    MM::PreviewSoundCue cue;
    cue.frame = 10; cue.style = MARCUS_SOUND_EFFECT;
    auto read = [](const MM::PreviewSoundCue &) {
        MarcusWaveformInfo audio = {};
        audio.kind = MARCUS_WAVEFORM_SAMPLED; audio.seconds = 20;
        return audio;
    };
    const MMMovieTimingScroll scrolls[] = {{11, 2}};
    const MMMovieTimingPause pauses[] = {{12, 3, 500}, {13, 2, 0}};
    MMMovieTimingHolds timing{4, 1, 1, scrolls, pauses};
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
    timing.pause_count = 2;
    timeline.Append(first, {cue}, read, timing);
    CHECK(std::abs(timeline.Scene(0).TimeAt(16) - 20.5) < 1e-6);
    timeline.Append(later, {}, read);
    CHECK(timeline.Resume(1, -5).empty()); // Sound-wait audio has completed.
    MM::PreviewPresentation presentation;
    MM::Error error;
    MarcusSoundTiming authored;
    for (int transition = 0; transition <= 5; ++transition)
    {
        presentation.transition = transition;
        CHECK(MarcusPlayback::ReadTiming(presentation, authored, error));
        const double expected = transition == 4 ? 1.0 / 6 : transition > 0 && transition < 4 ? 4 : 0;
        CHECK(authored.transition == expected);
    }
}
ChunkNumber Caption(PChunkyFile file, bool scrolling, bool edge = false, int layer = -1)
{
    RichTextData text;
    text.fontName.SetSzs(PszLit("Arial"));
    text.defaults.dypFont = 18;
    text.defaults.dxpDef = 400;
    text.defaults.lwAcrBack = scrolling ? 0x01000000 : 0x01ffffff;
    if (layer >= 0) text.defaults.lwAcrBack = layer == 0 ? 0x01000000 : layer == 1 ? 0x01ff0000 : 0x0100ff00;
    text.textOsk = koskUniWin;
    const std::u16string content = layer >= 0 ? u"" : edge ? u"E" : scrolling ? u"Credits one\rCredits two\rCredits three\rCredits four\rCredits five"
                                             : u"Marcus captions \u00e9\u03a9\rA second line";
    text.text.pfil = FileObject::PfilCreateTemp();
    text.text.cb = static_cast<LegacyLong>(content.size() * 2);
    CHECK(text.text.pfil && text.text.pfil->FSetFpMac(text.text.cb));
    CHECK(text.text.FWriteRgb(const_cast<char16_t *>(content.data()), text.text.cb, 0));
    text.properties = mm_text_collection_new(MM_TEXT_PROPERTIES, nullptr);
    CHECK(text.properties);
    RichTextPropertyEntry style = {0x01000000, 1}; // Bold.
    RichTextPropertyEntry foreground = {0x03000000, scrolling ? 0xffffff : 0x0000ff};
    RichTextPropertyEntry alignment = {0x80000000, 0x02000000}; // Centered.
    CHECK(text.FAddProperty(style) && text.FAddProperty(foreground) && text.FAddProperty(alignment));
    ChunkIdentification rich;
    CHECK(text.FWrite(file, &rich));
    TextBoxData box;
    box.SetBounds(20, 20, 460, scrolling ? 55 : 110);
    if (edge)
        box.SetBounds(500, 230, 544, 306);
    box.state.first = scrolling ? 0 : -2;
    box.state.end = scrolling ? 5 : -1;
    if (layer >= 0) { box.state.first = layer; box.state.end = 4; }
    box.state.story = !scrolling;
    ChunkNumber number;
    CHECK(file->FAdd(0, kctgTbox, &number) && box.FWrite(file, number));
    CHECK(file->FAdoptChild(kctgTbox, number, rich.ctg, rich.cno, 0));
    return number;
}
void CaptionStacking(const char *path, const char *resources, const char *content)
{
    String name;
    name.SetSzs(const_cast<char *>(path));
    Filename filename;
    CHECK(filename.FBuildFromPath(&name));
    auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    CHECK(file);
    ChunkNumber root, sceneNumber;
    CHECK(MovieData::FCreateRoot(file, &root));
    SceneData scene;
    scene.firstFrame = 0; scene.lastFrame = 3; scene.transition = transCut;
    CHECK(scene.FCreate(file, &sceneNumber) && file->FAdoptChild(kctgMvie, root, kctgScen, sceneNumber, 0));
    auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
    auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
    CHECK(starts && frames);
    for (unsigned slot = 0; slot < 3; ++slot)
    {
        auto box = Caption(file, false, false, 2 - slot);
        SceneEvent event = {0, sevtAddTbox};
        CHECK(file->FAdoptChild(kctgScen, sceneNumber, kctgTbox, box, slot));
        CHECK(starts->FInsert(starts->IvMac(), sizeof(slot), &slot, &event));
    }
    CHECK(SceneData::FWriteEvents(file, sceneNumber, frames, starts));
    CHECK(file->FSave(kctgSoc));
    ReleasePpo(&starts); ReleasePpo(&frames); ReleasePpo(&file);
    player = marcus_create();
    CHECK(player && marcus_open(player, path, resources, content));
    const auto check = [&](long frame, unsigned char red, unsigned char green) {
        CHECK(marcus_seek(player, frame));
        MarcusImage image;
        CHECK(marcus_render(player, 544, 306, 1, 0, 0, 1, &image));
        const auto pixel = image.rgba + 80 * image.pitch + 100 * 4;
        CHECK(pixel[0] == red && pixel[1] == green && pixel[2] == 0);
    };
    check(0, 0, 0);
    check(1, 255, 0);
    check(2, 0, 255); // New visibility brings green above red, regardless of list order.
    check(0, 0, 0);
    check(2, 255, 0); // Direct seek creates green then red: red is frontmost.
    check(3, 255, 0); // Repeated visibility retains the existing order.
    {
        std::unique_ptr<MarcusCaptionList, decltype(&marcus_captions_destroy)> list(
            marcus_captions(player), marcus_captions_destroy);
        CHECK(list && marcus_caption_at(player, list.get(), 100, 80) == 1);
        marcus_caption_preview(player, list.get(), 1);
        check(3, 0, 255); // The inline edit hides just this rendered box.
        CHECK(marcus_caption_at(player, list.get(), 100, 80) == 0);
        marcus_caption_preview(player, nullptr, -1);
        check(3, 255, 0);
        marcus_caption_preview(player, list.get(), 1);
        CHECK(marcus_open(player, path, resources, content));
        CHECK(marcus_caption_at(player, list.get(), 100, 80) == -1);
        check(3, 255, 0);
    }
    CHECK(marcus_seek(player, 0));
    const auto readCaptions = [&] {
        std::unique_ptr<MarcusCaptionList, decltype(&marcus_captions_destroy)> list(
            marcus_captions(player), marcus_captions_destroy);
        CHECK(list != nullptr);
        return list;
    };
    auto list = readCaptions();
    CHECK(marcus_caption_count(list.get()) == 3);
    MarcusCaptionOptions value;
    CHECK(marcus_caption(list.get(), -1, &value) && value.first_frame == 1 && value.through_end);
    value.x = 310; value.y = 205; value.width = 200; value.height = 48;
    value.first_frame = 2; value.last_frame = 3; value.through_end = 0;
    value.font_size = 18; value.bold = 1; value.italic = 1; value.alignment = 1;
    value.transparent = 0; value.paper = 0x112233; value.ink = 0xfedcba;
    const char *words = u8"New text\nUnicode \u03a9 and a line long enough to wrap";
    const auto captionPixels = [&] {
        MarcusImage image;
        CHECK(marcus_render(player, 544, 306, 1, 0, 0, 1, &image));
        return std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height);
    };
    CHECK(marcus_seek(player, 1));
    const auto clean = captionPixels();
    const auto document = marcus_document(player);
    CHECK(marcus_caption_draft(player, list.get(), -1, &value, words));
    int stopCount, scroll;
    const auto stops = marcus_caption_positions(player, &stopCount, &scroll);
    CHECK(stops && stopCount > 1 && scroll == 0 && stops[0].offset == 0 && stops[0].x > value.x + 14);
    CHECK(stops[stopCount - 1].offset == static_cast<int>(std::strlen(words)));
    CHECK(marcus_caption_caret(player, stops[stopCount - 1].offset));
    marcus_caption_positions(player, &stopCount, &scroll);
    CHECK(scroll > 0 && marcus_caption_caret(player, -1));
    const auto draft = captionPixels();
    CHECK(draft != clean && marcus_document(player) == document);
    marcus_caption_preview(player, nullptr, -1);
    CHECK(captionPixels() == clean);
    CHECK(marcus_edit_caption(player, list.get(), -1, &value, words));
    CHECK(captionPixels() == draft);
    CHECK(!marcus_edit_caption(player, list.get(), -1, &value, words)); // Snapshot of prior document.
    list = readCaptions();
    CHECK(marcus_caption_count(list.get()) == 4);
    MarcusCaptionOptions observed;
    auto savedWords = marcus_caption(list.get(), 3, &observed);
    CHECK(savedWords && !std::strcmp(savedWords, words));
    CHECK(observed.x == 310 && observed.y == 205 && observed.width == 200 && observed.height == 48 &&
          observed.first_frame == 2 && observed.last_frame == 3 && !observed.through_end &&
          observed.font_size == 18 && observed.bold && observed.italic && observed.alignment == 1 &&
          observed.ink == 0xfedcba && !observed.transparent && observed.paper == 0x112233);
    for (long frame : {0, 1, 2, 3})
    {
        CHECK(marcus_seek(player, frame));
        MarcusImage image;
        CHECK(marcus_render(player, 544, 306, 1, 0, 0, 1, &image));
        const auto pixel = image.rgba + 211 * image.pitch + 316 * 4;
        CHECK((pixel[0] == 0x11 && pixel[1] == 0x22 && pixel[2] == 0x33) == (frame == 1 || frame == 2));
    }
    value.x = 285;
    value.scrolling = 1;
    const char *edited = u8"Edited\nUnicode \u03a9";
    CHECK(marcus_seek(player, 1));
    const auto originalPixels = captionPixels();
    auto moved = observed;
    moved.x -= 10;
    CHECK(marcus_caption_draft(player, list.get(), 3, &moved, words));
    const auto movedDraft = captionPixels();
    CHECK(movedDraft != originalPixels);
    CHECK(marcus_edit_caption(player, list.get(), 3, &moved, words));
    CHECK(captionPixels() == movedDraft);
    CHECK(marcus_undo(player));
    list = readCaptions();
    CHECK(marcus_caption_draft(player, list.get(), 3, &value, edited));
    const auto editedDraft = captionPixels();
    CHECK(marcus_edit_caption(player, list.get(), 3, &value, edited));
    CHECK(captionPixels() == editedDraft);
    CHECK(marcus_undo(player));
    list = readCaptions();
    savedWords = marcus_caption(list.get(), 3, &observed);
    CHECK(savedWords && !std::strcmp(savedWords, words) && observed.x == 310 && !observed.scrolling);
    CHECK(marcus_edit_caption(player, list.get(), 3, &value, edited));
    const auto exported = std::string(path) + ".text.3mm";
    CHECK(marcus_save_copy(player, exported.c_str()));
    CHECK(marcus_open(player, exported.c_str(), resources, content));
    list = readCaptions();
    savedWords = marcus_caption(list.get(), 3, &observed);
    CHECK(savedWords && !std::strcmp(savedWords, edited) && observed.x == 285 && observed.scrolling &&
          observed.bold && observed.italic);
    CHECK(marcus_edit_caption(player, list.get(), 3, nullptr, nullptr));
    CHECK(marcus_caption_count(readCaptions().get()) == 3);
    CHECK(marcus_undo(player) && marcus_caption_count(readCaptions().get()) == 4);
    marcus_destroy(player); player = nullptr;
    CHECK(std::remove(exported.c_str()) == 0);
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
    CHECK((mm_array_add(&track.Route()->rows, &point, nullptr) == MM_STORAGE_OK));
    ActorEvent::Base event = {ActorEvent::aetAdd, -2, {0, rZero, 0}};
    ActorEvent::Add add = {};
    CHECK(mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()), sizeof(add), &add, &event));
    event.aet = ActorEvent::aetActn;
    ActorEvent::Action action = {0, 0};
    CHECK(mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()), sizeof(action), &action, &event));
    event.aet = ActorEvent::aetStep;
    BRS step = 0;
    CHECK(mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()), sizeof(step), &step, &event));
    event.aet = ActorEvent::aetFreeze;
    std::int32_t frozen = 1;
    CHECK(mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()), sizeof(frozen), &frozen, &event));
    event.aet = ActorEvent::aetSnd;
    MMActorSound cue = {};
    cue.style = stySfx;
    cue.volume = 0x10000;
    cue.cel = 0;
    cue.child = 7;
    cue.tag = {0, nullptr, kctgMsnd, sound + 123}; // Resolve imported sound through SCEN child 7.
    CHECK(mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()), sizeof(cue), &cue, &event));
    cue.cel = ivNil;
    CHECK(mm_actor_events_insert(track.Events(), mm_actor_events_count(track.Events()), sizeof(cue), &cue, &event));
    DataBlock block;
    CHECK(file->FAddChild(kctgActr, number, 0, mm_actor_path_wire_size(track.Route()), kctgPath, &child, &block));
    CHECK(ActorData::FWritePath(track.Route(), &block));
    auto wire = ActorData::PeventsEncode(track.Events());
    CHECK(wire && file->FAddChild(kctgActr, number, 0, mm_actor_events_wire_size(wire), kctgGgae, &child, &block));
    CHECK(ActorData::FWriteEvents(wire, &block));
    mm_actor_events_release(wire);
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
    auto title = Caption(file, false), credits = Caption(file, true), edge = Caption(file, false, true);
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
        for (unsigned slot = 0; slot < 3; ++slot)
        {
            SceneEvent event = {-2, sevtAddTbox};
            CHECK(file->FAdoptChild(kctgScen, number, kctgTbox, slot == 2 ? edge : slot ? credits : title, slot));
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
double FixtureScrollSeconds(const char *path, const char *resources, unsigned char palette[1024])
{
    MM::Archive archive;
    MM::Preview preview;
    MM::Error error;
    CHECK(archive.Open(path, error) && preview.Open(path, resources, error));
    for (const auto &chunk : archive.Chunks())
        if (chunk.id.type == kctgScen)
        {
            CHECK(preview.Select(chunk.id, error));
            CHECK(preview.CopyPresentationPalette(palette));
            MM::PreviewPresentation presentation;
            MarcusSoundTiming timing;
            CHECK(preview.ReadPresentation(presentation, error));
            CHECK(MarcusPlayback::ReadTiming(presentation, timing, error));
            for (std::size_t row = 0; row < timing.scroll_count; ++row)
                if (timing.scrolls[row].frame == 0)
                {
                    CHECK(timing.scrolls[row].seconds > 0);
                    return timing.scrolls[row].seconds;
                }
            break;
        }
    CHECK(false);
    return 0;
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
    CHECK(marcus_set_sound_track_mix(player, sound.track, 1, 0));
    CHECK(marcus_select_scene(player, 2) && marcus_sound(player, 0, &sound) && sound.carried);
    MarcusSoundTrack carriedTrack;
    CHECK(marcus_sound_track(player, sound.track, &carriedTrack) && carriedTrack.muted && !carriedTrack.audible);
    CHECK(marcus_select_scene(player, 0));
    CHECK(marcus_sound_track(player, 0, &carriedTrack) && carriedTrack.muted); // Same scene-effects lane at its origin.
    CHECK(marcus_select_scene(player, 2) && marcus_sound(player, 0, &sound) && sound.carried);
    const long originalVolume = sound.volume;
    const auto revision = marcus_document(player);
    MarcusSoundOptions edit = {-10, 12345, sound.style, sound.loop, sound.queue};
    CHECK(!marcus_set_sound(player, 0, &edit) && marcus_document(player) == revision);
    edit.frame = 0;
    CHECK(marcus_set_sound(player, 0, &edit));
    CHECK(marcus_info(player, &info) && info.scene == 2);
    CHECK(marcus_sound(player, 0, &sound) && sound.carried && sound.volume == edit.volume);
    CHECK(marcus_select_scene(player, 0) && marcus_sound(player, 0, &sound) && sound.volume == edit.volume);
    CHECK(marcus_undo(player) && marcus_select_scene(player, 0));
    CHECK(marcus_sound(player, 0, &sound) && sound.volume == originalVolume);
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
void PlaybackRates(const char *path, const char *resources, const char *content)
{
    player = marcus_create();
    CHECK(player && marcus_open(player, path, resources, content));
    const auto originalDuration = MovieTime().duration_seconds;
    for (int rate = 0; rate < 5; ++rate)
        for (int modifier = 0; modifier < 3; ++modifier)
        {
            CHECK(marcus_set_playback_rate(player, rate, modifier));
            CHECK(marcus_select_scene(player, 0) && marcus_seek(player, 3));
            const double fps = mm_playback_fps(rate, modifier);
            Tick(0);
            if (fps == 0)
                Tick(0); // Uncapped still visits every authored frame.
            else
            {
                const double interval = fps > 0 ? std::floor(1000 / fps) : 1000.0 / 6;
                Tick(interval / 2);
                CHECK(Info().frame == 3);
                Tick(10000, false);
                Tick(10000); // Resume discards elapsed time spent paused.
                CHECK(Info().frame == 3);
                Tick(interval / 2 + 1);
            }
            CHECK(Info().frame == 4);
        }
    CHECK(marcus_set_playback_rate(player, 3, 0));
    CHECK(!marcus_set_playback_rate(player, -1, 0));
    CHECK(!marcus_set_playback_rate(player, 5, 0));
    CHECK(!marcus_set_playback_rate(player, 1, 3));
    CHECK(marcus_select_scene(player, 0) && marcus_seek(player, 3));
    Tick(0);
    Tick(42);
    CHECK(Info().frame == 4); // Invalid settings retained 24 FPS.
    CHECK(marcus_seek(player, -2));
    Tick(0);
    Tick(10000);
    CHECK(Info().frame == -1); // Late callback advances one frame only.
    CHECK(Tick(0).wait == 1); // Click wait remains a real wait.
    CHECK(Tick(10000).wait == 1 && Info().frame == -1);
    Tick(0, true, true);
    CHECK(Info().frame == -1);
    CHECK(marcus_seek(player, 1));
    CHECK(Tick(0).wait == 3);
    CHECK(Tick(399).wait == 3 && Info().frame == 1);
    Tick(1);
    CHECK(Info().frame == 1); // Timed hold retains its ordinary 400 ms.
    Tick(42);
    CHECK(Info().frame == 2);
    CHECK(marcus_seek(player, 4));
    Image();
    Tick(0);
    CHECK(Tick(42).scene_changed);
    CHECK(Tick(3999).transition);
    CHECK(!Tick(1).transition); // Scene dissolve remains four seconds at 24 FPS.
    // Rebuild duration, waveforms and seek offsets using the selected rate.
    const auto fastDuration = MovieTime().duration_seconds;
    CHECK(std::abs(originalDuration - fastDuration - 35 * (1.0 / 6 - 1.0 / 24)) < 1e-6);
    CHECK(marcus_set_playback_rate(player, 2, 1));
    BackgroundTiming(); // The isolated helper must receive both preferences.
    const auto asyncTime = MovieTime();
    CHECK(marcus_set_playback_rate(player, 0, 0));
    CHECK(marcus_set_playback_rate(player, 2, 1));
    CHECK(std::abs(MovieTime().duration_seconds - asyncTime.duration_seconds) < 1e-6);
    CHECK(marcus_set_playback_rate(player, 4, 2));
    CHECK(std::abs(MovieTime().duration_seconds - originalDuration) < 1e-6); // Uncapped duration is nominal.
    marcus_destroy(player);
    player = nullptr;
}
void PlaybackCadence(const char *path, const char *resources, const char *content)
{
    const std::string ratePath = std::string(path) + ".fps.3mm";
    String name;
    name.SetSzs(const_cast<char *>(ratePath.c_str()));
    Filename filename;
    CHECK(filename.FBuildFromPath(&name));
    auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    CHECK(file);
    ChunkNumber root, number;
    CHECK(MovieData::FCreateRoot(file, &root));
    SceneData scene;
    scene.firstFrame = 0; scene.lastFrame = 1000; scene.transition = transCut;
    CHECK(scene.FCreate(file, &number) && file->FAdoptChild(kctgMvie, root, kctgScen, number, 0));
    auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
    auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
    CHECK(starts && frames && SceneData::FWriteEvents(file, number, frames, starts));
    ReleasePpo(&starts); ReleasePpo(&frames);
    CHECK(file->FSave(kctgSoc));
    ReleasePpo(&file);
    player = marcus_create();
    CHECK(player && marcus_open(player, ratePath.c_str(), resources, content));
    for (int rate = 0; rate < 5; ++rate)
        for (int modifier = 0; modifier < 3; ++modifier)
        {
            CHECK(marcus_set_playback_rate(player, rate, modifier) && marcus_seek(player, 0));
            Tick(0);
            for (int display = 0; display < 600; ++display)
                Tick(1000.0 / 60); // Ten seconds of ordinary 60 Hz host updates.
            const double fps = mm_playback_fps(rate, modifier);
            const double expected = fps == 0 ? 600 : 10 * (fps < 0 ? 6 : fps);
            CHECK(std::abs(Info().frame - expected) <= 1);
        }
    CHECK(marcus_set_playback_rate(player, 3, 0) && marcus_seek(player, 0));
    Tick(0);
    Tick(10000);
    CHECK(Info().frame == 1);
    Tick(1);
    CHECK(Info().frame == 1); // A stall cannot cause a burst of overdue frames.
    CHECK(marcus_seek(player, 0));
    Tick(0);
    Tick(65);
    CHECK(Info().frame == 1);
    Tick(40);
    CHECK(Info().frame == 1); // A late explicit-rate frame starts a fresh interval.
    Tick(2);
    CHECK(Info().frame == 2);
    CHECK(marcus_set_playback_rate(player, 0, 0) && marcus_seek(player, 0));
    Tick(0);
    Tick(250);
    CHECK(Info().frame == 1);
    Tick(160);
    CHECK(Info().frame == 1); // Original starts a fresh 6 FPS interval after the stall.
    Tick(7);
    CHECK(Info().frame == 2);
    marcus_destroy(player);
    player = nullptr;
    CHECK(filename.FDelete());
}
int FindSoundTrack(bool actorSound, long actor, long style)
{
    for (int index = 0; index < Info().sound_track_count; ++index)
    {
        MarcusSoundTrack track;
        CHECK(marcus_sound_track(player, index, &track));
        if ((track.actor_sound != 0) == actorSound && track.actor_id == actor && track.style == style)
            return index;
    }
    CHECK(false);
    return -1;
}
MarcusSoundTrack SoundTrack(int index)
{
    MarcusSoundTrack track;
    CHECK(marcus_sound_track(player, index, &track));
    return track;
}
MarcusAudioStats AudioStats()
{
    MarcusAudioStats stats;
    CHECK(marcus_audio_stats(player, &stats));
    return stats;
}
void SoundMixControls(const char *path, const char *resources, const char *content)
{
    player = marcus_create();
    CHECK(player && !marcus_sound_mix_active(player) && !marcus_sound_mix_active(nullptr));
    marcus_reset_sound_mix(nullptr);
    CHECK(marcus_open(player, path, resources, content));
    const int effects = FindSoundTrack(false, 0, MARCUS_SOUND_EFFECT);
    const int music = FindSoundTrack(false, 0, MARCUS_SOUND_MUSIC);
    CHECK(!SoundTrack(music).muted && !SoundTrack(music).solo && SoundTrack(music).audible);
    CHECK(!marcus_set_sound_track_mix(nullptr, 0, 1, 0));
    CHECK(!marcus_set_sound_track_mix(player, -1, 1, 0));
    CHECK(!marcus_set_sound_track_mix(player, Info().sound_track_count, 1, 0));
    CHECK(!marcus_set_sound_track_mix(player, music, 2, 0));
    CHECK(!marcus_set_sound_track_mix(player, music, 0, -1));
    CHECK(!marcus_sound_mix_active(player));
    CHECK(marcus_set_sound_track_mix(player, music, 1, 0));
    BackgroundTiming(); // Rebuilding the sound sheet must preserve controls.
    CHECK(SoundTrack(music).muted && !SoundTrack(music).audible);
    CHECK(marcus_seek(player, 0));
    Tick(0);
    SDL_Delay(150);
    const auto quiet = AudioStats();
    SDL_Delay(100);
    CHECK(AudioStats().audible_frames == quiet.audible_frames);
    CHECK(AudioStats().rendered_frames > quiet.rendered_frames && AudioStats().midi_sounds == 1);
    CHECK(marcus_set_sound_track_mix(player, music, 0, 0));
    SDL_Delay(150);
    CHECK(AudioStats().audible_frames > quiet.audible_frames && AudioStats().midi_sounds == 1);
    // Solo suppresses already-playing MIDI without stopping its voice.
    CHECK(marcus_set_sound_track_mix(player, effects, 0, 1));
    CHECK(!SoundTrack(music).audible && SoundTrack(effects).solo && SoundTrack(effects).audible);
    SDL_Delay(100);
    const auto soloQuiet = AudioStats().audible_frames;
    SDL_Delay(100);
    CHECK(AudioStats().audible_frames == soloQuiet);
    CHECK(marcus_set_sound_track_mix(player, music, 0, 1));
    CHECK(SoundTrack(effects).audible && SoundTrack(music).audible); // Multiple solos coexist.
    SDL_Delay(100);
    CHECK(AudioStats().audible_frames > soloQuiet && AudioStats().midi_sounds == 1);
    CHECK(marcus_set_sound_track_mix(player, music, 1, 1));
    CHECK(SoundTrack(music).solo && SoundTrack(music).muted && !SoundTrack(music).audible);
    marcus_reset_sound_mix(player);
    CHECK(!marcus_sound_mix_active(player) && SoundTrack(music).audible && SoundTrack(effects).audible);
    CHECK(AudioStats().midi_sounds == 1); // Toggling/resetting does not recreate voices.
    // Muted sampled queues still finish and release authored sound waits.
    CHECK(marcus_set_sound_track_mix(player, effects, 1, 0));
    CHECK(marcus_set_sound_track_mix(player, music, 1, 0));
    CHECK(marcus_seek(player, 2));
    CHECK(Tick(0).wait == 2 && AudioStats().wave_sounds == 2);
    const auto waiting = AudioStats().audible_frames;
    CHECK(Tick(10000).wait == 2 && Info().frame == 2);
    SDL_Delay(400);
    CHECK(Tick(0).wait == 0 && AudioStats().audible_frames == waiting);
    CHECK(marcus_select_scene(player, 1));
    CHECK(SoundTrack(FindSoundTrack(false, 0, MARCUS_SOUND_MUSIC)).muted);
    CHECK(marcus_set_playback_rate(player, 2, 0));
    CHECK(SoundTrack(FindSoundTrack(false, 0, MARCUS_SOUND_MUSIC)).muted);
    marcus_stop(player);
    CHECK(marcus_select_scene(player, 4));
    const int actor = FindSoundTrack(true, 42, MARCUS_SOUND_EFFECT);
    CHECK(marcus_set_sound_track_mix(player, actor, 0, 1));
    CHECK(SoundTrack(actor).audible && !SoundTrack(FindSoundTrack(false, 0, MARCUS_SOUND_EFFECT)).audible);
    CHECK(marcus_select_scene(player, 0) && marcus_seek(player, -2));
    CHECK(marcus_sound_mix_active(player) && !SoundTrack(music).audible); // Solo survives an absent layer.
    marcus_reset_sound_mix(player);
    CHECK(!marcus_sound_mix_active(player) && SoundTrack(music).audible);
    CHECK(marcus_select_scene(player, 4) && !SoundTrack(FindSoundTrack(true, 42, MARCUS_SOUND_EFFECT)).solo);
    CHECK(marcus_set_sound_track_mix(player, FindSoundTrack(false, 0, MARCUS_SOUND_MUSIC), 1, 0));
    CHECK(marcus_open(player, path, resources, content)); // Reopen begins a new movie session.
    CHECK(!marcus_sound_mix_active(player) && !SoundTrack(music).muted && SoundTrack(music).audible);
    marcus_destroy(player);
    player = nullptr;
}
void BundledFonts(const char *path, const char *resources, const char *content, const char *fontPath)
{
    if (!fontPath || !*fontPath) return; // Uses a local Arial fixture, never a redistributed font binary.
    auto font = Bytes(fontPath);
    auto u16 = [&](std::size_t at) { CHECK(at + 2 <= font.size()); return unsigned(font[at]) * 256 + font[at + 1]; };
    auto u32 = [&](std::size_t at) { return (std::uint32_t(u16(at)) << 16) | u16(at + 2); };
    std::size_t hmtx = 0, hhea = 0;
    for (unsigned table = 0; table < u16(4); ++table)
    {
        const std::size_t at = 12 + table * 16;
        if (u32(at) == 0x686d7478) hmtx = u32(at + 8);
        if (u32(at) == 0x68686561) hhea = u32(at + 8);
    }
    CHECK(hmtx && hhea);
    // Widen the local test font's advances to make accidental host-font fallback
    // observable in both caption pixels and scrolling duration.
    for (unsigned metric = 0; metric < u16(hhea + 34); ++metric)
    {
        const auto at = hmtx + metric * 4;
        const auto advance = std::min(65535u, u16(at) * 8);
        font[at] = byte(advance >> 8); font[at + 1] = byte(advance);
    }
    const auto zipPath = std::string(path) + ".fonts.zip";
    const auto movie = Bytes(path);
    mz_zip_archive zip = {};
    CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
    CHECK(mz_zip_writer_add_mem(&zip, "nested/movie.3mm", movie.data(), movie.size(), 6));
    CHECK(mz_zip_writer_add_mem(&zip, "fonts/renamed.TTF", font.data(), font.size(), 6));
    void *zipBytes = nullptr; std::size_t zipLength = 0;
    CHECK(mz_zip_writer_finalize_heap_archive(&zip, &zipBytes, &zipLength));
    { std::ofstream out(zipPath, std::ios::binary); out.write(static_cast<const char *>(zipBytes), zipLength); CHECK(bool(out)); }
    mz_free(zipBytes); mz_zip_writer_end(&zip);
    const auto originalZip = Bytes(zipPath.c_str());
    player = marcus_create();
    CHECK(player && marcus_open(player, path, resources, content));
    const auto installed = Image();
    const auto installedTime = MovieTime().duration_seconds;
    CHECK(marcus_open(player, zipPath.c_str(), resources, content));
    const auto bundled = Image();
    CHECK(bundled != installed);
    BackgroundTiming();
    const auto bundledTime = MovieTime().duration_seconds;
    CHECK(bundledTime > installedTime); // Background helper uses the wider ZIP font too.
    MarcusImage native;
    CHECK(marcus_render(player, 160, 120, 1, 0, 0, 1, &native));
    const std::vector<byte> expected(native.rgba, native.rgba + native.pitch * native.height);
    auto previews = marcus_frame_previews_create(player);
    CHECK(previews);
    const long first = Info().frame;
    marcus_frame_previews_request(previews, &first, 1);
    int status = 0; char error[256] = {};
    const auto deadline = SDL_GetTicks() + 15000;
    while (!status && SDL_GetTicks() < deadline)
    {
        status = marcus_frame_previews_take(previews, &native, error, sizeof(error));
        if (!status) SDL_Delay(5);
    }
    CHECK(status == 1 && native.width == 160 && native.height == 120);
    CHECK(std::vector<byte>(native.rgba, native.rgba + native.pitch * native.height) == expected);
    marcus_frame_previews_destroy(previews);
    CHECK(marcus_open(player, path, resources, content) && Image() == installed);
    CHECK(std::abs(MovieTime().duration_seconds - installedTime) < 1e-6); // No font scope leaks into the next movie.
    marcus_destroy(player); player = nullptr;
    CHECK(Bytes(zipPath.c_str()) == originalZip);
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
    if (argc != 4 && argc != 5)
        return 2;
    WaveformPeaks();
    FailedTiming();
    CarryScheduling();
    RangeScheduling();
    PresentationClock();
    InvalidMovieHeaders(argv[1], argv[2], argv[3]);
    Fixture(argv[1]);
    unsigned char fixturePalette[1024];
    const double authoredScroll = FixtureScrollSeconds(argv[1], argv[2], fixturePalette);
    const auto movieBefore = Bytes(argv[1]), resourcesBefore = Bytes(argv[2]);
    PlaybackRates(argv[1], argv[2], argv[3]);
    PlaybackCadence(argv[1], argv[2], argv[3]);
    SoundMixControls(argv[1], argv[2], argv[3]);
    BundledFonts(argv[1], argv[2], argv[3], argc == 5 ? argv[4] : nullptr);
    player = marcus_create();
    CHECK(player && marcus_open(player, argv[1], argv[2], argv[3]));
    // Reopening cancels and joins a pending job before releasing its leases.
    MarcusMovieTime pending;
    CHECK(marcus_movie_time_async(player, &pending) == 0);
    CHECK(marcus_open(player, argv[1], argv[2], argv[3]));
    BackgroundTiming();
    CHECK(Info().first_frame == -2 && Info().object_count == 0);
    CHECK(!marcus_movie_time(nullptr, nullptr) && !marcus_movie_time(player, nullptr));
    const double sceneSeconds = 7.0 / 6 + authoredScroll + .4 + .2;
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
    // The rightmost 64 authored pixels must remain visible, without shifting
    // a box past the viewport or stretching it from a 480x360 coordinate plane.
    const auto canvasTitle = Image(1, 544, 306);
    const auto edgePixel = std::size_t(270 * 544 + 530) * 4;
    CHECK(canvasTitle[edgePixel] == 255 && canvasTitle[edgePixel + 1] == 255 &&
          canvasTitle[edgePixel + 2] == 255 && canvasTitle[edgePixel + 3] == 255);
    CHECK(canvasTitle[std::size_t(220 * 544 + 530) * 4 + 3] == 0);
    // Enlarged captions must contain newly rasterized, smoothed font edges,
    // rather than replicas of the native glyph pixels. Layout/time stays native.
    const auto captionFrame = Info().frame;
    const auto sharpTitle = Image(1, 1440, 1080);
    std::size_t smoothEdges = 0, changedPixels = 0;
    for (int y = 0; y < 1080; ++y)
        for (int x = 0; x < 1440; ++x)
        {
            const auto pixel = std::size_t(y * 1440 + x) * 4;
            const auto native = std::size_t((y / 3) * 480 + x / 3) * 4;
            smoothEdges += sharpTitle[pixel] > 0 && sharpTitle[pixel] < 255 && sharpTitle[pixel + 2] == 255;
            changedPixels += std::memcmp(&sharpTitle[pixel], &title[native], 4) != 0;
        }
    CHECK(smoothEdges > 100 && changedPixels > 100);
    CHECK(Info().frame == captionFrame && Image() == title);
    CHECK(Image(1, 1440, 1080) == sharpTitle);
    const auto sharpWorkTitle = Image(0, 1440, 1080);
    smoothEdges = 0;
    for (std::size_t pixel = 0; pixel < sharpWorkTitle.size(); pixel += 4)
        smoothEdges += sharpWorkTitle[pixel] > 0 && sharpWorkTitle[pixel] < 255 &&
                       sharpWorkTitle[pixel + 2] == 255;
    CHECK(smoothEdges > 100 && Image(0, 1440, 1080) == sharpWorkTitle);
    CHECK(Image() == title && Info().frame == captionFrame);
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
    const auto scrollAlarms = std::lround(authoredScroll * 12);
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
        {
            // The three title-only scenes share this palette. Retail remaps
            // palette colours when taking the snapshot, even before any reveal.
            auto snapshot = outgoing;
            const MMPixelSurface surface{snapshot.data(), 480, 360, 480 * 4, 32};
            CHECK(mm_pixel_remap_dissolve_rgba(&surface, fixturePalette, 256, nullptr));
            CHECK(initial == snapshot);
        }
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
    CHECK(marcus_seek_time(player, 1.0 / 3 + authoredScroll / 2));
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
    CaptionStacking((std::string(argv[1]) + ".stack.3mm").c_str(), argv[2], argv[3]);
    std::puts(
        "Headless playback: caption visibility/rewind, Unicode, scrolling, click/time/sound waits, MIDI/WAVE audio,");
    std::puts("queued clips, outgoing scene transitions, looping, pause/resume, and unchanged input files.");
    return 0;
}
