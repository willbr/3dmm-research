#include "sound_edit.h"
#include "scene_edit.h"
#include "archive_file.h"
#include <SDL3/SDL.h>
#include "soc_core.h"
#include "sdl_audio.h"
#include "mm/native_sound.h"
#include "private_archive_host.h"
#include <algorithm>
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
struct AudioInput
{
    std::string extension;
    byte wire[kcbMaxDataStn];
    MarcusSoundImport value{};
    explicit AudioInput(const char *path)
    {
        if (!path) return;
        const auto full = std::filesystem::u8path(path);
        auto filename = Path(full);
        String name;
        filename.GetLeaf(&name);
        extension = full.extension().u8string();
        value = {path, extension.c_str(), MarcusArchiveName(name, wire),
            [](const MMReadStream *source, MMI32 style, const MMChunkNameValue *title,
               MMU32 *number, const MMArchiveWriter *writer) -> int {
                try { return KauaiAudio::ImportWave(*source, style, *title, number, *writer); }
                catch (...) { return MM_WAVE_BAD_AUDIO; }
            }};
    }
};
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
    const auto profile = KauaiPrivateArchiveProfile();
    const AudioInput input(path);
    const auto error = marcus_sound_preview_write(result->path.u8string().c_str(), &profile, &input.value, style);
    Require(!error, error);
    return result;
}
std::shared_ptr<MarcusMovieFile> MarcusEditSound(const std::shared_ptr<MarcusMovieFile> &movie,
    const MarcusSoundEditPlan &plan, const char *importPath, const char *libraryPath, MMU32 libraryChunk)
{
    auto result = MarcusCopyMovie(movie);
    const auto profile = KauaiPrivateArchiveProfile();
    const AudioInput input(importPath);
    const auto error = marcus_sound_edit_write(result->path.u8string().c_str(), &profile, &plan,
        importPath ? &input.value : nullptr, libraryPath, libraryChunk);
    Require(!error, error);
    return result;
}
