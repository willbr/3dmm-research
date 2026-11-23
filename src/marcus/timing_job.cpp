#include "timing_job.h"
#include "timing_worker.h"
#include <exception>
#include <stdexcept>
#include <utility>

struct MarcusTimingJob::Impl
{
    std::shared_ptr<MarcusMovieFile> movie;
    std::string resources, content, input, fonts, origin;
    std::filesystem::path root, staging, candidate;
    std::string output;
    std::exception_ptr stageFailure;
    // Join before destroying the paths and movie lease borrowed by the worker.
    std::unique_ptr<MarcusTimingWorker, decltype(&marcus_timing_worker_close)> worker{nullptr, marcus_timing_worker_close};

    Impl(std::shared_ptr<MarcusMovieFile> file, std::string resources, std::string content,
         const MarcusScene *scenes, std::size_t count, int rate, int modifier)
        : movie(std::move(file)), resources(std::move(resources)), content(std::move(content)),
          input(movie->path.u8string()), fonts(movie->fontDirectory.u8string()), origin(movie->contentOrigin.u8string())
    {
        const MarcusTimingInput request{input.c_str(), this->resources.c_str(), this->content.c_str(),
                                        fonts.c_str(), origin.c_str(), scenes, count, rate, modifier};
        const MarcusTimingFiles files{this, Reserve, Discard};
        const char *failure;
        worker.reset(marcus_timing_worker_start(&request, &files, &failure));
        if (!worker)
        {
            if (failure) throw std::runtime_error(failure);
            throw std::bad_alloc();
        }
    }

    static MMMovieStageResult Reserve(void *p, const char *directory, const char *leaf, const char **path)
    {
        auto &host = *static_cast<Impl *>(p);
        try
        {
            if (host.root.empty()) host.root = std::filesystem::temp_directory_path();
            auto staging = host.root / directory;
            auto candidate = staging / leaf;
            auto output = candidate.u8string();
            if (!std::filesystem::create_directory(staging))
                return MM_MOVIE_STAGE_COLLISION;
            host.staging = std::move(staging);
            host.candidate = std::move(candidate);
            host.output = std::move(output);
            *path = host.output.c_str();
            return MM_MOVIE_STAGE_OK;
        }
        catch (...)
        {
            host.stageFailure = std::current_exception();
            return MM_MOVIE_STAGE_ERROR;
        }
    }

    static void Discard(void *p)
    {
        const auto &host = *static_cast<const Impl *>(p);
        std::error_code ignored;
        std::filesystem::remove(host.candidate, ignored);
        std::filesystem::remove(host.staging, ignored);
    }
};

MarcusTimingJob::MarcusTimingJob(std::shared_ptr<MarcusMovieFile> movie, std::string resources, std::string content,
                                 const MarcusScene *scenes, std::size_t count, int rate, int modifier)
    : impl(std::make_unique<Impl>(std::move(movie), std::move(resources), std::move(content), scenes, count, rate, modifier))
{
}

MarcusTimingJob::~MarcusTimingJob() = default;

bool MarcusTimingJob::Take(std::unique_ptr<MarcusSoundTimeline> &timeline, std::string &error)
{
    MMMovieSoundSchedule *schedule;
    const char *failure;
    if (!marcus_timing_worker_take(impl->worker.get(), &schedule, &failure))
        return false;
    MarcusSoundTimeline::Schedule owner(schedule, mm_movie_sound_schedule_destroy);
    if (owner)
        timeline.reset(new MarcusSoundTimeline(std::move(owner)));
    else
        timeline.reset();
    error = failure ? failure : "";
    if (impl->stageFailure)
    {
        try { std::rethrow_exception(impl->stageFailure); }
        catch (const std::exception &exception) { error = exception.what(); }
        catch (...) { }
    }
    return true;
}
