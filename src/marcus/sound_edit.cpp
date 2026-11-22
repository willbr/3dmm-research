#include "sound_edit.h"
#include "scene_edit.h"
#include "archive_file.h"
#include <SDL3/SDL.h>
#include "soc_core.h"
#include "sdl_audio.h"
#include "mm/sound_resource.h"
#include "mm/native_sound.h"
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
void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
Filename Path(const std::filesystem::path &path)
{
    String text; Filename result; text.SetSzs(const_cast<char *>(path.u8string().c_str()));
    Require(result.FBuildFromPath(&text), "Invalid sound file path."); return result;
}
ChunkNumber Import(MMPrivateArchive *file, const char *path, int style)
{
    auto filename = Path(std::filesystem::u8path(path));
    std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> input(SDL_IOFromFile(path, "rb"), SDL_CloseIO);
    Require(bool(input), "Cannot open the audio file.");
    const auto length = SDL_GetIOSize(input.get());
    Require(length > 0 && length <= 64 * 1024 * 1024, "Audio files must be between 1 byte and 64 MiB.");
    const MMReadStream source = {input.get(), static_cast<MMU32>(length),
        [](void *file, MMU32 offset, void *bytes, size_t count) -> int {
            auto stream = static_cast<SDL_IOStream *>(file);
            return SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) == offset && SDL_ReadIO(stream, bytes, count) == count;
        }};
    auto extension = std::filesystem::u8path(path).extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const bool midi = extension == ".mid" || extension == ".midi";
    Require(midi == (style == MARCUS_SOUND_MUSIC), "Music cues use MIDI files; WAV/MP3 audio uses Effects or Speech.");
    String name;
    filename.GetLeaf(&name);
    byte wire[kcbMaxDataStn];
    const auto title = MarcusArchiveName(name, wire);
    auto writer = mm_private_archive_writer(file);
    writer.big_endian = 0;
    ChunkNumber sound;
    int written;
    if (midi)
    {
        written = mm_sound_import_midi(&source, &title, &sound, &writer);
        Require(written != MM_SOUND_BAD_MIDI, "Cannot import this MIDI file.");
    }
    else
    {
        written = KauaiAudio::ImportWave(source, style, title, &sound, writer);
        Require(written != MM_WAVE_BAD_AUDIO, "Cannot decode this WAV or MP3 file.");
    }
    Require(written > 0, "Cannot encode this sound in the native movie format.");
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
    Require(archive.OpenForInspection(path.u8string(), error),
            "Cannot open the built-in sound library (snds.3cn). Check the content directory.");
    std::vector<MarcusSoundAsset> result;
    for (const auto &chunk : archive.Chunks())
    {
        if (chunk.id.type != kctgMsnd) continue;
        std::int32_t style;
        if (!MM::ReadSoundCatalogStyle(archive, chunk.id.number, style))
            continue;
        MarcusSoundAsset asset = {}; asset.chunk = chunk.id.number; asset.style = style;
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
    auto result = MarcusCreateEventFile();
    auto file = MarcusOpenArchive(result->path, MM_PRIVATE_ARCHIVE_CREATE);
    Require(file && Import(file.get(), path, style) == 0 && mm_private_archive_save(file.get(), kctgSoc),
            "Cannot prepare this audio file.");
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusEditSound(const std::shared_ptr<MarcusMovieFile> &movie,
    unsigned long scene, const MM::PreviewSoundCue *cue, const MarcusSoundOptions &options, const char *importPath, bool remove,
    const char *libraryPath, unsigned long libraryChunk)
{
    auto result = MarcusCopyMovie(movie);
    auto file = MarcusOpenArchive(result->path, MM_PRIVATE_ARCHIVE_EDIT);
    Require(bool(file), "Cannot open the sound working copy.");
    ChunkNumber added = 0;
    if (libraryPath)
    {
        auto library = MarcusOpenArchive(std::filesystem::u8path(libraryPath), MM_PRIVATE_ARCHIVE_READ);
        Require(library && mm_private_archive_copy(library.get(), file.get(), kctgMsnd, libraryChunk, 0, &added),
                "Cannot copy the selected built-in sound.");
    }
    else if (importPath)
        added = Import(file.get(), importPath, options.style);
    MMMovieSoundArchiveCue selected{};
    if (cue)
        selected = {cue->frame, cue->source, cue->actor, cue->style, cue->motionCel,
                    cue->child, cue->sound.number, cue->queue, cue->actorSound};
    const MMMovieSoundArchiveEdit edit{cue ? &selected : nullptr, (importPath || libraryPath) ? &added : nullptr,
        static_cast<MMI32>(options.frame), static_cast<MMI32>(options.volume), options.style, options.loop, options.queue, remove};
    const auto editor = mm_private_archive_editor(file.get());
    CheckSoundArchive(mm_movie_archive_edit_sound(&editor, static_cast<MMU32>(scene), &edit));
    Require(mm_private_archive_save(file.get(), kctgSoc), "Cannot save sound edits.");
    return result;
}
