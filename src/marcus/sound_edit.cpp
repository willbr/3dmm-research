#include "sound_edit.h"
#include "scene_edit.h"
#include "movie_archive_host.h"
#include <SDL3/SDL.h>
#include "soc_core.h"
#include "midi.h"
#include "sdl_audio.h"
#include "mm/sound_resource.h"
#include "mm/movie_sound_archive.h"
#include <algorithm>
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
void CheckSoundArchive(int status)
{
    static const char *const messages[] = {
        nullptr,
        "Cannot find movie root.",
        "Cannot link imported sound.",
        "Cannot retain imported sound.",
        "Cannot read scene sound events.",
        "Invalid scene events.",
        "Cannot decode or encode the scene sound packet.",
        "The sound cue could not be found.",
        "Cannot allocate the sound edit.",
        "Cannot write scene sound events.",
        "Cannot link scene sound events.",
        "Cannot read sound owner.",
        "Cannot read actor sound track.",
        "Cannot write actor sound events.",
        "Cannot copy sound owner or its children.",
        "Cannot link sound owner."
    };
    static_assert(sizeof(messages) / sizeof(messages[0]) == MM_SOUND_ARCHIVE_LINK_ACTOR + 1,
                  "sound archive diagnostics");
    Require(status == MM_SOUND_ARCHIVE_OK, messages[status]);
}
}
std::vector<MarcusSoundAsset> MarcusReadSoundAssets(const std::filesystem::path &path)
{
    MM::Archive archive; MM::Error error;
    Require(archive.Open(path.u8string(), error), "Cannot open the built-in sound library (snds.3cn). Check the content directory.");
    auto filename = Path(path);
    std::unique_ptr<ChunkyFile, Release> file(ChunkyFile::PcflOpen(&filename, fcflNil));
    Require(bool(file), "Cannot read the built-in sound library.");
    struct Info { MMSoundResource resource = {}; MMI32 silent = 0; unsigned char invalid = 0; } info;
    DataBlock block; MMSoundResourceServices services = {}; services.context = &block;
    services.owner = [](void *, void *p, MMSoundOwner *out) {
        auto &i = *static_cast<Info *>(p); *out = {&i.resource, &i.silent, &i.invalid};
    };
    services.find = [](void *p, void *f, MMU32 type, MMU32 number) -> void * {
        auto b = static_cast<DataBlock *>(p); return static_cast<PChunkyFile>(f)->FFind(type, number, b) ? b : nullptr;
    };
    services.unpack = [](void *, void *p) -> int { return static_cast<DataBlock *>(p)->FUnpackData(); };
    services.block_size = [](void *, void *p) -> MMI32 { return static_cast<DataBlock *>(p)->Cb(); };
    services.read = [](void *, void *p, void *out, MMI32 bytes) -> int { return static_cast<DataBlock *>(p)->FReadRgb(out, bytes, 0); };
    services.read_name = [](void *, void *, MMU32, MMU32, void *) { return 1; }; // Archive already decoded UTF-8 names.
    services.child = [](void *, void *f, MMU32 type, MMU32 number, MMSoundChild *out) -> int {
        ChildChunkIdentification child;
        for (int i = 0; static_cast<PChunkyFile>(f)->FGetKid(type, number, i, &child); ++i)
            if (child.cki.ctg == kctgWave || child.cki.ctg == kctgMidi) { *out = {child.cki.ctg, child.cki.cno}; return 1; }
        return 0;
    };
    std::vector<MarcusSoundAsset> result;
    for (const auto &chunk : archive.Chunks())
    {
        if (chunk.id.type != kctgMsnd) continue;
        info = {};
        if (!mm_sound_read(&info, file.get(), kctgMsnd, chunk.id.number, &services) || info.invalid || info.silent ||
            info.resource.style < MARCUS_SOUND_EFFECT || info.resource.style > MARCUS_SOUND_MUSIC) continue;
        MarcusSoundAsset asset = {}; asset.chunk = chunk.id.number; asset.style = info.resource.style;
        const auto name = chunk.name.empty() ? "Sound " + std::to_string(asset.chunk) : chunk.name;
        std::strncpy(asset.name, name.c_str(), sizeof(asset.name) - 1); result.push_back(asset);
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.style != b.style ? a.style < b.style : SDL_strcasecmp(a.name, b.name) < 0;
    });
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusPrepareSound(const char *path, int style)
{
    auto result = MarcusCreateEventFile(); auto filename = Path(result->path);
    std::unique_ptr<ChunkyFile, Release> file(ChunkyFile::PcflCreate(&filename, fcflWriteEnable));
    Require(file && Import(file.get(), path, style) == 0 && file->FSave(kctgSoc), "Cannot prepare this audio file.");
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusEditSound(const std::shared_ptr<MarcusMovieFile> &movie,
    unsigned long scene, const MM::PreviewSoundCue *cue, const MarcusSoundOptions &options, const char *importPath, bool remove,
    const char *libraryPath, unsigned long libraryChunk)
{
    auto result = MarcusCopyMovie(movie); auto filename = Path(result->path);
    std::unique_ptr<ChunkyFile, Release> file(ChunkyFile::PcflOpen(&filename, fcflWriteEnable));
    Require(bool(file), "Cannot open the sound working copy.");
    ChunkNumber added = 0;
    if (libraryPath)
    {
        auto name = Path(std::filesystem::u8path(libraryPath));
        std::unique_ptr<ChunkyFile, Release> library(ChunkyFile::PcflOpen(&name, fcflNil));
        Require(library && library->FCopy(kctgMsnd, libraryChunk, file.get(), &added), "Cannot copy the selected built-in sound.");
    }
    else if (importPath)
        added = Import(file.get(), importPath, options.style);
    MMMovieSoundArchiveCue selected{};
    if (cue)
        selected = {cue->frame, cue->source, cue->actor, cue->style, cue->motionCel,
                    cue->child, cue->sound.number, cue->queue, cue->actorSound};
    const MMMovieSoundArchiveEdit edit{cue ? &selected : nullptr, (importPath || libraryPath) ? &added : nullptr,
        static_cast<MMI32>(options.frame), static_cast<MMI32>(options.volume), options.style, options.loop, options.queue, remove};
    MovieArchiveHost host{file.get(), {}, {}};
    const auto editor = host.Editor();
    CheckSoundArchive(mm_movie_archive_edit_sound(&editor, static_cast<MMU32>(scene), &edit));
    Require(file->FSave(kctgSoc), "Cannot save sound edits.");
    return result;
}
