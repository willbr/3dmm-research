#include "sound_edit.h"
#include "scene_edit.h"
#include "archive_file.h"
#include "soc_core.h"
#include "sdl_audio.h"
#include "platform_text.h"
#include "private_archive_host.h"
#include <stdexcept>
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
        value = {path, extension.c_str(), MarcusArchiveName(name, wire), KauaiAudio::ImportWave};
    }
};
}
void MarcusLoadSoundCatalog(MarcusSoundCatalog &catalog, const char *directory)
{
    if (marcus_sound_catalog_matches(&catalog, directory)) return;
    const auto path = (std::filesystem::u8path(directory) / "snds.3cn").u8string();
    const auto profile = KauaiPrivateArchiveProfile();
    const auto status = marcus_sound_catalog_load(&catalog, directory, path.c_str(), &profile, KauaiText::Services());
    if (status == MARCUS_SOUND_CATALOG_MEMORY) throw std::bad_alloc();
    Require(status == MARCUS_SOUND_CATALOG_OK,
            "Cannot open the built-in sound library (snds.3cn). Check the content directory.");
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
