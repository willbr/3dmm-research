// Licensed under the MIT License.
#include "mm/v3dmm.h"
#include "mm/movie_publish.h"
#include <exception>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace MM::V3dmm
{
namespace
{
namespace fs = std::filesystem;
void ReplaceFile(const fs::path &candidate, const fs::path &destination)
{
#ifdef _WIN32
    if (!MoveFileExW(candidate.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish v3dmm file");
#else
    std::filesystem::rename(candidate, destination);
#endif
}

struct SaveStage
{
    const fs::path destination;
    const std::function<void(const fs::path &)> &write;
    fs::path directory, candidate;
    std::exception_ptr error;
    std::random_device random{};

    static MMMovieStageResult Reserve(void *state) noexcept
    {
        auto &stage = *static_cast<SaveStage *>(state);
        try
        {
            stage.directory = stage.destination.parent_path() / (".3dmm-save-" + std::to_string(stage.random()));
            stage.candidate = stage.directory / "movie.tmp";
            std::error_code ec;
            if (fs::create_directory(stage.directory, ec)) return MM_MOVIE_STAGE_OK;
            if (!ec || ec == std::errc::file_exists) return MM_MOVIE_STAGE_COLLISION;
            throw fs::filesystem_error("Cannot reserve movie save staging", stage.directory, ec);
        }
        catch (...)
        {
            stage.error = std::current_exception();
            return MM_MOVIE_STAGE_ERROR;
        }
    }

    static int Write(void *state) noexcept
    {
        auto &stage = *static_cast<SaveStage *>(state);
        try { stage.write(stage.candidate); return 1; }
        catch (...) { stage.error = std::current_exception(); return 0; }
    }

    static int Replace(void *state) noexcept
    {
        auto &stage = *static_cast<SaveStage *>(state);
        try { ReplaceFile(stage.candidate, stage.destination); return 1; }
        catch (...) { stage.error = std::current_exception(); return 0; }
    }

    static void Discard(void *state) noexcept
    {
        const auto &stage = *static_cast<SaveStage *>(state);
        std::error_code ignored;
        fs::remove(stage.candidate, ignored);
        fs::remove(stage.directory, ignored);
    }
};
} // namespace

void Publish(const std::filesystem::path &candidate, const std::filesystem::path &destination)
{
    if (fs::weakly_canonical(candidate.parent_path()) != fs::weakly_canonical(destination.parent_path()))
        throw std::runtime_error("V3dmm publication must stay on the same filesystem");
    ReplaceFile(candidate, destination);
}

void SaveAtomically(const std::filesystem::path &destination,
                    const std::function<void(const std::filesystem::path &)> &write)
{
    SaveStage stage{fs::absolute(destination), write, {}, {}, {}};
    const MMMoviePublishServices services = {&stage, SaveStage::Reserve, SaveStage::Write, SaveStage::Replace,
                                              SaveStage::Discard};
    const auto result = mm_movie_publish_save(&services);
    if (stage.error) std::rethrow_exception(stage.error);
    if (result != MM_MOVIE_PUBLISH_OK)
        throw std::runtime_error("Cannot reserve movie save staging");
}
} // namespace MM::V3dmm
