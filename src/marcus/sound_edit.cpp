#include "sound_edit.h"
#include "scene_edit.h"
#include "soc_core.h"
#include "midi.h"
#include "sdl_audio.h"
#include "mm/sound_resource.h"
#include "mm/scene_sound.h"
#include "mm/movie_sound.h"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <vector>
#undef size
ASSERTNAME
namespace
{
struct Release { template<class T> void operator()(T *p) const { if (p) p->Release(); } };
void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
Filename Path(const std::filesystem::path &path)
{
    String text; Filename result; text.SetSzs(const_cast<char *>(path.u8string().c_str()));
    Require(result.FBuildFromPath(&text), "Invalid sound file path."); return result;
}
ChunkNumber Import(PChunkyFile file, const char *path, int style)
{
    auto filename = Path(std::filesystem::u8path(path));
    std::unique_ptr<FileObject, Release> input(FileObject::PfilOpen(&filename));
    Require(bool(input), "Cannot open the audio file.");
    Require(input->FpMac() > 0 && input->FpMac() <= 64 * 1024 * 1024, "Audio files must be between 1 byte and 64 MiB.");
    auto extension = std::filesystem::u8path(path).extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const bool midi = extension == ".mid" || extension == ".midi";
    Require(midi == (style == MARCUS_SOUND_MUSIC), "Music cues use MIDI files; WAV/MP3 audio uses Effects or Speech.");
    struct Context { std::vector<unsigned char> wave; DataBlock block; } context;
    std::unique_ptr<MidiStream, Release> music;
    if (midi) { music.reset(MidiStream::PmidsReadNative(&filename)); Require(bool(music), "Cannot import this MIDI file."); }
    else
    {
        std::vector<unsigned char> bytes(input->FpMac());
        Require(input->FReadRgb(bytes.data(), bytes.size(), 0) && KauaiAudio::PrepareWaveImport(bytes.data(), bytes.size(), context.wave),
            "Cannot decode this WAV or MP3 file.");
    }
    MMSoundResourceServices services = {}; services.context = &context; services.system = koskCur;
    services.add = [](void *, void *f, const void *header, void *number) -> int {
        return static_cast<PChunkyFile>(f)->FAddPv(const_cast<void *>(header), 16, kctgMsnd, static_cast<ChunkNumber *>(number));
    };
    services.wave_size = [](void *p, void *) -> MMI32 { return MMI32(static_cast<Context *>(p)->wave.size()); };
    services.midi_size = [](void *, void *p) -> MMI32 { return static_cast<MidiStream *>(p)->CbOnFile(); };
    services.add_child = [](void *p, void *f, MMU32 parent, MMU32 type, MMI32 bytes, MMU32 *out) -> int {
        return static_cast<PChunkyFile>(f)->FAddChild(kctgMsnd, parent, 0, bytes, type, out, &static_cast<Context *>(p)->block);
    };
    services.find_wave = [](void *, void *, MMU32) { return 1; };
    services.copy_wave = [](void *p) -> int { auto &c = *static_cast<Context *>(p); return c.block.FWriteRgb(c.wave.data(), c.wave.size(), 0); };
    services.write_midi = [](void *p, void *source) -> int { return static_cast<MidiStream *>(source)->FWrite(&static_cast<Context *>(p)->block); };
    services.set_name = [](void *, void *f, MMU32 number, const void *name) -> int {
        return static_cast<PChunkyFile>(f)->FSetName(kctgMsnd, number, const_cast<String *>(static_cast<const String *>(name)));
    };
    services.delete_parent = [](void *, void *f, MMU32 number) { static_cast<PChunkyFile>(f)->Delete(kctgMsnd, number); };
    String name; filename.GetLeaf(&name); ChunkNumber sound;
    Require(mm_sound_write(midi ? static_cast<void *>(music.get()) : nullptr, !midi, file, style, &name, &sound, &services) != 0,
        "Cannot encode this sound in the native movie format.");
    return sound;
}
struct WireTag { std::uint32_t child; TAGOnFile tag; };
static_assert(sizeof(WireTag) == 20, "Sound packet tag layout");
MMSceneSoundPacketServices Packets()
{
    MMSceneSoundPacketServices s = {}; s.tag_bytes = sizeof(WireTag);
    s.allocate = [](void *, MMI32 bytes) -> void * { return std::malloc(bytes); };
    s.copy_record = [](void *, void *a, const void *b) { std::memcpy(a, b, sizeof(WireTag)); };
    s.sid = [](void *, const void *p) -> MMI32 { return static_cast<const WireTag *>(p)->tag.sid; };
    s.retain = [](void *, void *) {}; s.clear_cache = [](void *, void *) {};
    s.read_tag = [](void *, const void *p, MMU32 *child, MMTag *tag) {
        const auto &v = *static_cast<const WireTag *>(p); *child = v.child;
        *tag = {v.tag.sid, nullptr, v.tag.ctg, v.tag.cno};
    };
    s.write_tag = [](void *, void *p, MMU32 child, const MMTag *tag) {
        *static_cast<WireTag *>(p) = {child, {tag->sid, 0, tag->type, tag->number}};
    };
    return s;
}
using Packet = std::unique_ptr<MMSceneSound, decltype(&std::free)>;
Packet Decode(const std::vector<unsigned char> &bytes, bool swap)
{
    auto copy = bytes; const auto services = Packets();
    Packet result(mm_scene_sound_decode(copy.data(), MMI32(copy.size()), swap, &services), std::free);
    Require(bool(result), "Invalid scene sound packet."); return result;
}
std::vector<unsigned char> Encode(const MMSceneSound *packet, bool swap)
{
    const auto services = Packets(); MMI32 bytes;
    std::unique_ptr<void, decltype(&std::free)> wire(mm_scene_sound_encode(packet, &bytes, &services), std::free);
    Require(bool(wire), "Cannot encode the sound cue.");
    std::vector<unsigned char> result(static_cast<unsigned char *>(wire.get()), static_cast<unsigned char *>(wire.get()) + bytes);
    if (swap) SwapBytesRglw(result.data(), bytes / 4);
    return result;
}
bool Matches(const MM::PreviewSoundCue &cue, const WireTag &tag, int index)
{
    return cue.source == tag.tag.sid && (cue.source == 0 ? cue.child == tag.child : cue.sound.number == tag.tag.cno) &&
        cue.queue == (index > 0);
}
struct Row { SceneEvent event; std::vector<unsigned char> bytes; };
void SceneCue(PChunkyFile file, ChunkNumber scene, const MM::PreviewSoundCue *cue,
              const MarcusSoundOptions &options, const WireTag *added, bool remove)
{
    ChildChunkIdentification child; DataBlock block; short order;
    Require(file->FGetKidChidCtg(kctgScen, scene, 0, kctgFrmGg, &child) && file->FFind(kctgFrmGg, child.cki.cno, &block), "Cannot read scene sound events.");
    std::unique_ptr<GeneralGroup, Release> input(SceneData::PggReadEvents(&block, &order, false));
    Require(bool(input), "Invalid scene events."); const bool swap = order == kboOther;
    std::vector<Row> rows; bool changed = false;
    for (int i = 0; i < input->IvMac(); ++i)
    {
        Row row; input->GetFixed(i, &row.event); if (swap) SwapBytesBom(&row.event, kbomSev);
        row.bytes.resize(input->Cb(i)); input->Get(i, row.bytes.data());
        bool drop = false;
        if (row.event.sevt == sevtPlaySnd && ((cue && row.event.nfrm == cue->frame) || (added && row.event.nfrm == options.frame)))
        {
            auto packet = Decode(row.bytes, swap);
            if (packet->sty == (cue ? cue->style : options.style) && !changed)
            {
                auto tags = reinterpret_cast<WireTag *>(packet.get() + 1); int match = -1;
                if (cue) for (int j = 0; j < packet->ctagc; ++j) if (Matches(*cue, tags[j], j)) { match = j; break; }
                if (match >= 0)
                {
                    changed = true;
                    if (remove)
                    {
                        std::memmove(tags + match, tags + match + 1, (--packet->ctagc - match) * sizeof(WireTag));
                        drop = !packet->ctagc;
                    }
                    else { packet->vlm = options.volume; packet->sty = options.style; packet->fLoop = options.loop; row.event.nfrm = options.frame; }
                    if (!drop) row.bytes = Encode(packet.get(), swap);
                }
                else if (added)
                {
                    changed = true;
                    if (options.queue && !options.loop)
                    {
                        const auto services = Packets(); MMTag tag = {0, nullptr, kctgMsnd, added->tag.cno};
                        Packet next(mm_scene_sound_append_tag(packet.get(), added->child, &tag, &services), std::free);
                        Require(bool(next), "Cannot queue the sound."); row.bytes = Encode(next.get(), swap);
                    }
                    else
                    {
                        const auto services = Packets();
                        Packet next(mm_scene_sound_make(options.volume, options.style, options.loop, 1, added, &services), std::free);
                        Require(bool(next), "Cannot add the sound."); row.bytes = Encode(next.get(), swap);
                    }
                }
            }
        }
        if (!drop) rows.push_back(std::move(row));
    }
    if (added && !changed)
    {
        const auto services = Packets();
        Packet packet(mm_scene_sound_make(options.volume, options.style, options.loop, 1, added, &services), std::free);
        Require(bool(packet), "Cannot create sound cue.");
        rows.push_back({{int32_t(options.frame), sevtPlaySnd}, Encode(packet.get(), swap)}); changed = true;
    }
    Require(changed, "The sound cue could not be found.");
    std::stable_sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.event.nfrm < b.event.nfrm; });
    std::unique_ptr<GeneralGroup, Release> output(GeneralGroup::PggNew(sizeof(SceneEvent)));
    Require(bool(output), "Cannot allocate scene events.");
    for (auto &row : rows)
    {
        if (swap) SwapBytesBom(&row.event, kbomSev);
        Require(output->FInsert(output->IvMac(), row.bytes.size(), row.bytes.data(), &row.event), "Cannot update scene events.");
    }
    ChunkNumber number;
    Require(file->FAdd(output->CbOnFile(), kctgFrmGg, &number, &block) && output->FWrite(&block, order), "Cannot write scene sound events.");
    file->DeleteChild(kctgScen, scene, kctgFrmGg, child.cki.cno, 0);
    Require(file->FAdoptChild(kctgScen, scene, kctgFrmGg, number, 0), "Cannot link scene sound events.");
}
void ActorCue(PChunkyFile file, ChunkNumber scene, const MM::PreviewSoundCue &cue, const MarcusSoundOptions &options, bool remove)
{
    ChildChunkIdentification link, child; DataBlock block; ChunkNumber actor = 0; bool found = false;
    for (int i = 0; file->FGetKid(kctgScen, scene, i, &link); ++i) if (link.cki.ctg == kctgActr)
    {
        ActorChunkOnFile header;
        Require(file->FFind(kctgActr, link.cki.cno, &block) && ActorData::FReadHeader(&block, &header), "Cannot read sound owner.");
        if (header.arid == cue.actor) { actor = link.cki.cno; found = true; break; }
    }
    Require(found && file->FGetKidChidCtg(kctgActr, actor, 0, kctgGgae, &child) &&
        file->FFind(kctgGgae, child.cki.cno, &block), "Cannot read actor sound track.");
    ActorTrack track; Require(track.AdoptEvents(ActorData::PggReadEvents(&block)) != nullptr, "Invalid actor sound events.");
    auto events = track.Events(); found = false;
    for (int i = 0; i < events->IvMac(); ++i)
    {
        ActorEvent::Base event; events->GetFixed(i, &event);
        if (event.aet != ActorEvent::aetSnd || event.nfrm != cue.frame) continue;
        ActorEvent::Sound sound; events->Get(i, &sound);
        if (sound.sty != cue.style || sound.celn != cue.motionCel || sound.tag.sid != cue.source ||
            (cue.source == 0 ? sound.chid != cue.child : sound.tag.cno != cue.sound.number) || bool(sound.fQueue) != cue.queue) continue;
        if (remove) events->Delete(i);
        else { sound.vlm = options.volume; sound.fLoop = tribool(options.loop); sound.fQueue = tribool(options.queue); events->Put(i, &sound); }
        found = true; break;
    }
    Require(found, "Cannot find the actor's sound binding.");
    std::unique_ptr<GeneralGroup, Release> wire(ActorData::PggWriteEvents(events)); ChunkNumber number;
    Require(wire && file->FAdd(wire->CbOnFile(), kctgGgae, &number, &block) && wire->FWrite(&block), "Cannot write actor sound events.");
    // Materialization has already isolated edited actor instances. Sound-only
    // changes also clone the root so another scene sharing ACTR stays intact.
    ActorChunkOnFile header; Require(file->FFind(kctgActr, actor, &block) && ActorData::FReadHeader(&block, &header), "Cannot read actor header.");
    ChunkNumber copy; Require(file->FAdd(sizeof(header), kctgActr, &copy) && ActorData::FWriteHeader(file, copy, &header), "Cannot copy sound owner.");
    for (int i = 0; file->FGetKid(kctgActr, actor, i, &child); ++i)
        Require(file->FAdoptChild(kctgActr, copy, child.cki.ctg, child.cki.ctg == kctgGgae && child.chid == 0 ? number : child.cki.cno, child.chid), "Cannot retain actor children.");
    file->DeleteChild(kctgScen, scene, kctgActr, actor, link.chid);
    Require(file->FAdoptChild(kctgScen, scene, kctgActr, copy, link.chid), "Cannot link sound owner.");
}
}
std::shared_ptr<MarcusMovieFile> MarcusPrepareSound(const char *path, int style)
{
    auto result = MarcusCreateEventFile(); auto filename = Path(result->path);
    std::unique_ptr<ChunkyFile, Release> file(ChunkyFile::PcflCreate(&filename, fcflWriteEnable));
    Require(file && Import(file.get(), path, style) == 0 && file->FSave(kctgSoc), "Cannot prepare this audio file.");
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusEditSound(const std::shared_ptr<MarcusMovieFile> &movie,
    unsigned long scene, const MM::PreviewSoundCue *cue, const MarcusSoundOptions &options, const char *importPath, bool remove)
{
    auto result = MarcusCopyMovie(movie); auto filename = Path(result->path);
    std::unique_ptr<ChunkyFile, Release> file(ChunkyFile::PcflOpen(&filename, fcflWriteEnable));
    Require(bool(file), "Cannot open the sound working copy.");
    WireTag added = {}; ChunkIdentification root;
    if (importPath)
    {
        const auto sound = Import(file.get(), importPath, options.style);
        Require(file->FGetCkiCtg(kctgMvie, 0, &root), "Cannot find movie root.");
        MMU32 slot = 0; ChildChunkIdentification child;
        while (file->FGetKidChidCtg(kctgScen, scene, slot, kctgMsnd, &child)) ++slot;
        Require(file->FAdoptChild(kctgScen, scene, kctgMsnd, sound, slot), "Cannot link imported sound.");
        added = {slot, {0, 0, kctgMsnd, sound}};
        slot = 0; while (file->FGetKidChidCtg(kctgMvie, root.cno, slot, kctgMsnd, &child)) ++slot;
        Require(file->FAdoptChild(kctgMvie, root.cno, kctgMsnd, sound, slot), "Cannot retain imported sound.");
    }
    if (cue && cue->actorSound) ActorCue(file.get(), scene, *cue, options, remove);
    else SceneCue(file.get(), scene, cue, options, importPath ? &added : nullptr, remove);
    Require(file->FSave(kctgSoc), "Cannot save sound edits.");
    return result;
}
