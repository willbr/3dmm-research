#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include "player.h"
#include "soc_core.h"
#include <SDL3/SDL.h>
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
ChunkNumber Sound(PChunkyFile file, bool midi)
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
        const unsigned samples = 4800;
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
                bool queued = false)
{
    SceneEvent event = {frame, sevtPlaySnd};
    std::int32_t data[] = {0x10000,
                           midi ? styMidi : stySfx,
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
    Fixture(argv[1]);
    const auto movieBefore = Bytes(argv[1]), resourcesBefore = Bytes(argv[2]);
    player = marcus_create();
    CHECK(player && marcus_open(player, argv[1], argv[2], argv[3]));
    CHECK(Info().first_frame == -2 && Info().object_count == 0);
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
    Tick(10000);
    CHECK(Info().frame == -1);
    Tick(0, false);
    CHECK(Tick(10000).wait == 1); // Resume does not consume a click pause.
    CHECK(Tick(0, true, true).wait == 0);
    Tick(170);
    CHECK(Info().frame == 0);
    CHECK(Tick(0).scrolling);
    Tick(100);
    CHECK(Info().frame == 0 && Image() != credits);
    MarcusAudioStats stats;
    CHECK(marcus_audio_stats(player, &stats) && stats.midi_sounds == 1);
    SDL_Delay(150);
    CHECK(marcus_audio_stats(player, &stats) && stats.audible_frames > 0);
    for (int i = 0; i < 200 && Info().frame == 0; ++i)
        Tick(100);
    CHECK(Info().frame == 1);
    CHECK(Tick(0).wait == 3);
    Tick(200);
    Tick(0, false);
    Tick(10000); // User pause freezes the remaining authored duration.
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
        auto state = Tick(170);
        CHECK(state.scene_changed && state.transition && Info().scene == scene + 1);
        CHECK(marcus_audio_stats(player, &stats) && stats.midi_sounds == musicBeforeTransition);
        const auto initial = Image();
        if (scene < 3)
            CHECK(initial == outgoing);
        Tick(scene == 3 ? 80 : 1000);
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
    Tick(0, true, false, true);
    CHECK(Tick(170, true, false, true).scene_changed && Info().scene == 0 && Info().frame == -2);
    CHECK(!marcus_update(player, 1, 0, -1, 0, nullptr));
    marcus_destroy(player);
    player = nullptr;
    CHECK(Bytes(argv[1]) == movieBefore && Bytes(argv[2]) == resourcesBefore);
    std::puts(
        "Headless playback: caption visibility/rewind, Unicode, scrolling, click/time/sound waits, MIDI/WAVE audio,");
    std::puts("queued clips, outgoing scene transitions, looping, pause/resume, and unchanged input files.");
    return 0;
}
