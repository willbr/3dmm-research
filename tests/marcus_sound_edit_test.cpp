// Native sound authoring, save and undo checks; no application UI.
#include "player.h"
#include "storyboard.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "soc_core.h"
#undef size
ASSERTNAME
namespace {
void Require(bool ok, const char *message, MarcusPlayer *p = nullptr)
{ if (!ok) throw std::runtime_error(std::string(message) + (p ? std::string(": ") + marcus_error(p) : "")); }
void Word(std::vector<unsigned char> &b, unsigned v, int n)
{ for (int i = 0; i < n; ++i) b.push_back(static_cast<unsigned char>(v >> (i * 8))); }
void Wave(const std::string &path)
{
    std::vector<unsigned char> b = {'R','I','F','F'};
    Word(b, 36 + 4800 * 2, 4); b.insert(b.end(), {'W','A','V','E','f','m','t',' '});
    Word(b, 16, 4); Word(b, 1, 2); Word(b, 1, 2); Word(b, 48000, 4); Word(b, 96000, 4);
    Word(b, 2, 2); Word(b, 16, 2); b.insert(b.end(), {'d','a','t','a'}); Word(b, 4800 * 2, 4);
    for (int i = 0; i < 4800; ++i) Word(b, static_cast<unsigned short>(short(std::sin(i * .06) * 12000)), 2);
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<char *>(b.data()), b.size());
}
void Midi(const std::string &path)
{
    const unsigned char b[] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96,'M','T','r','k',0,0,0,12,
        0,0x90,60,100,96,0x80,60,0,0,0xff,0x2f,0};
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char *>(b), sizeof(b));
}
std::vector<unsigned char> Bytes(const std::string &path)
{ std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()}; }
// Attach both a motion-cel binding and an ordinary sound to the inserted actor.
void ActorSounds(const std::string &path)
{
    String s; s.SetSzs(const_cast<char *>(path.c_str())); Filename name;
    Require(name.FBuildFromPath(&s), "Actor fixture path");
    auto file = ChunkyFile::PcflOpen(&name, fcflWriteEnable); Require(file != nullptr, "Open actor fixture");
    ChunkIdentification scene; ChildChunkIdentification actor, events, sound;
    Require(file->FGetCkiCtg(kctgScen, 0, &scene) && file->FGetKidChidCtg(kctgScen, scene.cno, 0, kctgActr, &actor) &&
        file->FGetKidChidCtg(kctgActr, actor.cki.cno, 0, kctgGgae, &events) &&
        file->FGetKidChidCtg(kctgScen, scene.cno, 0, kctgMsnd, &sound), "Actor fixture links");
    DataBlock block; Require(file->FFind(kctgGgae, events.cki.cno, &block), "Read actor events");
    ActorTrack track; Require(track.AdoptEvents(ActorData::PggReadEvents(&block)) != nullptr, "Decode actor events");
    ActorEvent::Base event = {}; event.aet = ActorEvent::aetSnd; event.nfrm = 1;
    // Insertion's first event supplies the unchanged route/cel position.
    ActorEvent::Base add; track.Events()->GetFixed(0, &add); event.rtel = add.rtel;
    ActorEvent::Sound cue = {}; cue.sty = stySpeech; cue.vlm = 65536; cue.celn = ivNil;
    cue.chid = sound.chid; cue.tag = {0, nullptr, kctgMsnd, sound.cki.cno};
    Require(track.Events()->FInsert(track.Events()->IvMac(), sizeof(cue), &cue, &event), "Insert actor sound");
    cue.sty = stySfx; cue.celn = 0;
    Require(track.Events()->FInsert(track.Events()->IvMac(), sizeof(cue), &cue, &event), "Insert motion binding");
    auto wire = ActorData::PggWriteEvents(track.Events());
    Require(wire && file->FPut(wire->CbOnFile(), kctgGgae, events.cki.cno, &block) && wire->FWrite(&block) && file->FSave(kctgSoc), "Save actor fixture");
    ReleasePpo(&wire); block.Free(); ReleasePpo(&file);
}
}
int main(int argc, char **argv)
{
    if (argc != 4) return 2;
    auto p = std::unique_ptr<MarcusPlayer, decltype(&marcus_destroy)>(marcus_create(), marcus_destroy);
    auto check = [&](bool ok, const char *message) { Require(ok, message, p.get()); };
    try {
        const auto dir = std::filesystem::u8path(argv[3]); std::filesystem::create_directories(dir);
        const auto wave = (dir / "Test tone.wav").u8string(), midi = (dir / "Test music.mid").u8string();
        const auto saved = (dir / "sound.3mm").u8string(), actorMovie = (dir / "actor.3mm").u8string();
        Wave(wave); Midi(midi);
        check(marcus_add_scene(p.get(), 11, argv[1], argv[2]), "New scene");
        MarcusProp actor = {}; check(marcus_actor_count(p.get(), argv[2]) > 0 && marcus_actor(p.get(), 0, &actor), "Actor catalogue");
        check(marcus_add_actor(p.get(), actor.chunk) == 0, "Insert actor");
        MarcusObject before = {}; check(marcus_object(p.get(), 0, &before), "Actor before");
        check(marcus_move_object(p.get(), 0, before.x + 2, before.y, before.z), "Move actor");
        for (int i = 0; i < 3; ++i) check(marcus_add_frame(p.get(), 1), "Extend scene");
        MarcusSoundOptions options = {1, 32768, MARCUS_SOUND_EFFECT, 0, 1};
        auto revision = marcus_document(p.get());
        check(marcus_add_sound(p.get(), "missing-sound.wav", &options) < 0 && marcus_document(p.get()) == revision, "Failed import is atomic");
        check(marcus_audition_sound_file(p.get(), wave.c_str(), options.style, options.volume), "Preview file");
        check(marcus_document(p.get()) == revision, "Preview leaves movie unchanged"); marcus_stop_sound_preview(p.get());
        const int first = marcus_add_sound(p.get(), wave.c_str(), &options);
        check(first >= 0, "Import WAV");
        MarcusSound sound = {}; MarcusObject after = {}; MarcusInfo info = {};
        check(marcus_sound(p.get(), first, &sound) && std::string(sound.name) == "Test tone.wav" && sound.volume == 32768 && sound.frame == 1, "Imported name and settings");
        check(marcus_object(p.get(), 0, &after) && after.x == before.x + 2 && after.actor_id == before.actor_id, "Sound edit retains actor placement");
        check(marcus_info(p.get(), &info) && info.frame == 4 && info.last_frame == 4, "Sound edit retains frame range and selection");
        MarcusWaveformInfo audio = {}; MarcusWaveformColumn peaks[16] = {};
        check(marcus_sound_waveform(p.get(), first, 0, .01, 16, peaks, &audio) && audio.kind == MARCUS_WAVEFORM_SAMPLED && std::abs(audio.seconds - .1) < .001, "Embedded WAV decodes");
        check(marcus_audition_sound(p.get(), first, 16384), "Preview existing");
        MarcusPlaybackState playback = {}; check(marcus_update(p.get(), 0, 0, 20, 0, &playback), "Audition while paused"); marcus_stop_sound_preview(p.get());
        revision = marcus_document(p.get());
        check(marcus_set_sound(p.get(), first, &options) && marcus_document(p.get()) == revision, "Unchanged sound is no-op");
        auto invalid = options; invalid.frame = 9;
        check(!marcus_set_sound(p.get(), first, &invalid) && marcus_document(p.get()) == revision, "Reject out of scene timing");
        invalid = options; invalid.style = MARCUS_SOUND_MUSIC;
        check(!marcus_set_sound(p.get(), first, &invalid) && marcus_document(p.get()) == revision, "Reject sampled resource as MIDI");
        const int queued = marcus_add_sound(p.get(), wave.c_str(), &options);
        check(queued >= 0 && marcus_info(p.get(), &info) && info.sound_count == 2 && marcus_sound(p.get(), queued, &sound) && sound.queue, "Queue a second cue");
        options.frame = 2; options.volume = 16384; options.style = MARCUS_SOUND_SPEECH; options.loop = 1;
        check(marcus_set_sound(p.get(), queued, &options), "Edit queued group");
        for (int i = 0; i < 2; ++i) check(marcus_sound(p.get(), i, &sound) && sound.frame == 2 && sound.volume == 16384 && sound.style == MARCUS_SOUND_SPEECH && sound.loop, "Shared packet settings retained");
        check(marcus_remove_sound(p.get(), 1) && marcus_info(p.get(), &info) && info.sound_count == 1, "Remove only queued sound");
        check(marcus_undo(p.get()) && marcus_info(p.get(), &info) && info.sound_count == 2, "Undo removal");
        check(marcus_undo(p.get()) && marcus_sound(p.get(), 0, &sound) && sound.frame == 1 && sound.volume == 32768 && !sound.loop, "Undo group settings");
        check(marcus_undo(p.get()) && marcus_info(p.get(), &info) && info.sound_count == 1, "Undo queued import");
        check(marcus_save_copy(p.get(), actorMovie.c_str()), "Save actor fixture");
        check(marcus_undo(p.get()) && marcus_info(p.get(), &info) && !info.sound_count && info.object_count == 1 && info.last_frame == 4, "Undo sound to live unsaved scene");
        for (int i = 0; i < 3; ++i) check(marcus_undo(p.get()), "Undo earlier frame edit");
        check(marcus_undo(p.get()) && marcus_object(p.get(), 0, &after) && after.x == before.x, "Undo actor move before sound import");
        check(marcus_open(p.get(), actorMovie.c_str(), argv[1], argv[2]), "Reopen sound movie");
        MarcusProp font = {}; check(marcus_font_count(p.get(), argv[2]) > 0 && marcus_font(p.get(), 0, &font), "Font catalogue");
        const int text = marcus_add_text(p.get(), "Sound edit", font.chunk, 1);
        check(text >= 0, "Insert text before sound edit");
        MarcusImage image = {}; check(marcus_render(p.get(), 320, 180, 1, 0, 0, 1, &image), "Render before sound edit");
        const std::vector<unsigned char> originalPixels(image.rgba, image.rgba + image.pitch * image.height);
        options = {2, 49152, MARCUS_SOUND_MUSIC, 1, 0};
        check(marcus_add_sound(p.get(), midi.c_str(), &options) >= 0, "Import MIDI music");
        check(marcus_render(p.get(), 320, 180, 1, 0, 0, 1, &image) &&
            originalPixels == std::vector<unsigned char>(image.rgba, image.rgba + image.pitch * image.height), "Sound edit preserves actor/text render");
        check(marcus_save_copy(p.get(), saved.c_str()), "Save imported sounds");
        const auto original = Bytes(saved);
        std::filesystem::remove(wave); std::filesystem::remove(midi);
        check(marcus_open(p.get(), saved.c_str(), argv[1], argv[2]) && marcus_info(p.get(), &info) && info.sound_count == 2, "Reopen embedded sounds without original files");
        for (int i = 0; i < info.sound_count; ++i) {
            check(marcus_sound(p.get(), i, &sound), "Reopened sound");
            if (sound.style == MARCUS_SOUND_MUSIC) check(sound.loop && sound.volume == 49152 && marcus_audition_sound(p.get(), i, 49152), "Retained MIDI plays");
        }
        marcus_stop_sound_preview(p.get());
        check(marcus_remove_sound(p.get(), 0) && Bytes(saved) == original, "Editing leaves input file untouched");
        ActorSounds(actorMovie);
        check(marcus_open(p.get(), actorMovie.c_str(), argv[1], argv[2]) && marcus_info(p.get(), &info), "Open actor sound fixture");
        int edited = 0;
        for (int i = 0; i < info.sound_count; ++i) {
            check(marcus_sound(p.get(), i, &sound), "Actor fixture cue"); if (!sound.actor_sound) continue;
            options = {sound.frame, 22222, int(sound.style), 1, int(sound.queue)};
            check(marcus_set_sound(p.get(), i, &options) && marcus_sound(p.get(), i, &sound) && sound.volume == 22222 && sound.loop, "Edit actor volume and loop retaining binding");
            check(marcus_undo(p.get()), "Undo actor sound edit"); ++edited;
        }
        check(edited >= 2, "Both ordinary and motion actor sound bindings edited");
        Wave(wave);
        MarcusStoryboard board; board.angles[0].pixels[500] = 1;
        check(MarcusApplyStoryboard(p.get(), board, true, 0, argv[1], argv[2]), "Add storyboard before sound import");
        options = {1, 65536, MARCUS_SOUND_EFFECT, 0, 1};
        check(marcus_add_sound(p.get(), wave.c_str(), &options) >= 0 && marcus_is_storyboard(p.get()), "Storyboard remains editable after sound import");
        MarcusStoryboard restored; check(MarcusGetStoryboard(p.get(), restored) && restored.angles[0].pixels == board.angles[0].pixels, "Retain editable painted pixels");
        const auto bundle = (dir / "sound-board.vmm").u8string();
        check(marcus_save_copy(p.get(), bundle.c_str()) && marcus_open(p.get(), bundle.c_str(), argv[1], argv[2]), "Save and reopen sound storyboard bundle");
        check(marcus_select_scene(p.get(), 1) && marcus_is_storyboard(p.get()) && marcus_info(p.get(), &info) && info.sound_count > 0 &&
            MarcusGetStoryboard(p.get(), restored) && restored.angles[0].pixels == board.angles[0].pixels, "Bundled storyboard retains sound and painted pixels");
        std::puts("Sound import/edit/queue/remove/audition, native save, actor binding and undo checks passed.");
        return 0;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
