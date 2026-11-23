#include "timing_worker.h"
#include "archive_file.h"
#include "playback_scene.h"
#include "preview_process.h"
#include <stdlib.h>

struct MarcusTimingWorker
{
    MarcusTimingInput input;
    MarcusTimingFiles files;
    MMMovieTimingRange *ranges;
    MMMovieSoundSchedule *result;
    SDL_Thread *thread;
    SDL_AtomicInt cancelled, done;
    char *executable;
    const char *output, *failure;
    int valid_ranges;
};

static MMMovieStageResult reserve_output(void *p, const char *directory, const char *leaf)
{
    MarcusTimingWorker *job = p;
    return job->files.reserve(job->files.context, directory, leaf, &job->output);
}

typedef struct TimingSnapshot
{
    SDL_IOStream *file;
    const MMMovieTimingRange *ranges;
    size_t remaining;
} TimingSnapshot;

static int snapshot_read(void *p, void *out, size_t bytes)
{
    TimingSnapshot *snapshot = p;
    if (bytes > snapshot->remaining || SDL_ReadIO(snapshot->file, out, bytes) != bytes)
        return 0;
    snapshot->remaining -= bytes;
    return 1;
}

static int snapshot_end(void *p)
{
    const TimingSnapshot *snapshot = p;
    unsigned char extra;
    return snapshot->remaining == 0 && SDL_ReadIO(snapshot->file, &extra, 1) == 0 &&
           SDL_GetIOStatus(snapshot->file) == SDL_IO_STATUS_EOF;
}

static int snapshot_range(void *p, size_t row, MMMovieTimingRange *out)
{
    *out = ((const TimingSnapshot *)p)->ranges[row];
    return 1;
}

static const char *read_output(MarcusTimingWorker *job)
{
    TimingSnapshot snapshot;
    MMMovieSoundSnapshotInput input = {NULL, snapshot_read, snapshot_end, snapshot_range};
    Sint64 bytes;
    const char *failure = NULL;
    snapshot.file = SDL_IOFromFile(job->output, "rb");
    if (!snapshot.file)
        return "Cannot read movie timing output.";
    bytes = SDL_GetIOSize(snapshot.file);
    if (bytes < 0)
        failure = "Cannot read movie timing output.";
    else if (!bytes || bytes > 64UL * 1024 * 1024)
        failure = "Movie timing output exceeds its limit.";
    else
    {
        snapshot.ranges = job->ranges;
        snapshot.remaining = (size_t)bytes;
        input.context = &snapshot;
        if (!job->valid_ranges || !mm_movie_sound_schedule_read_snapshot(&input, job->input.count, &job->result))
            failure = "Invalid movie timing output.";
    }
    SDL_CloseIO(snapshot.file);
    return failure;
}

static const char *run_helper(MarcusTimingWorker *job)
{
    const MarcusTimingInput *input = &job->input;
    const char *args[11];
    char rate[32], modifier[32];
    SDL_Process *process;
    Uint64 start;
    int status = -1;
    const char *failure = NULL;
    SDL_snprintf(rate, sizeof(rate), "%d", input->rate);
    SDL_snprintf(modifier, sizeof(modifier), "%d", input->modifier);
    args[0] = job->executable;
    args[1] = input->movie;
    args[2] = input->resources;
    args[3] = "--timing";
    args[4] = input->content;
    args[5] = job->output;
    args[6] = rate;
    args[7] = modifier;
    args[8] = input->fonts;
    args[9] = input->origin;
    args[10] = NULL;
    process = marcus_preview_process_start(args, 0, 0);
    if (!process)
        return "Cannot launch marcus-preview for timing.";
    start = SDL_GetTicks();
    while (!SDL_WaitProcess(process, 0, &status))
    {
        if (SDL_GetAtomicInt(&job->cancelled) || SDL_GetTicks() - start >= 300000UL)
        {
            failure = "Movie timing cancelled or timed out.";
            break;
        }
        SDL_Delay(5);
    }
    marcus_preview_process_close(process);
    if (failure)
        return failure;
    return status ? "Movie timing analysis failed." : read_output(job);
}

static int SDLCALL timing_thread(void *p)
{
    MarcusTimingWorker *job = p;
    MarcusWorkingFiles stage = {NULL, reserve_output, NULL};
    char stamp[32];
    stage.context = job;
    if (SDL_GetAtomicInt(&job->cancelled))
        job->failure = "Movie timing cancelled or timed out.";
    else if (!job->executable)
        job->failure = "Cannot locate the timing helper.";
    else
    {
        SDL_snprintf(stamp, sizeof(stamp), "%" SDL_PRIu64, SDL_GetPerformanceCounter());
        if (!marcus_archive_stage_working_file(MARCUS_WORKING_TIMING, stamp, 0, &stage))
            job->failure = "Cannot reserve timing output.";
        else
        {
            job->failure = run_helper(job);
            job->files.discard(job->files.context);
        }
    }
    SDL_SetAtomicInt(&job->done, 1);
    return 0;
}

MarcusTimingWorker *marcus_timing_worker_start(const MarcusTimingInput *input, const MarcusTimingFiles *files,
                                               const char **failure)
{
    MarcusTimingWorker *job;
    size_t i;
    *failure = NULL;
    if (input->count > (size_t)-1 / sizeof(*job->ranges))
        return NULL;
    job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;
    job->input = *input;
    job->input.scenes = NULL;
    job->files = *files;
    job->valid_ranges = 1;
    if (input->count)
    {
        job->ranges = malloc(input->count * sizeof(*job->ranges));
        if (!job->ranges)
        {
            marcus_timing_worker_close(job);
            return NULL;
        }
        for (i = 0; i < input->count; ++i)
            if (!marcus_playback_scene_timing_range(input->scenes + i, job->ranges + i))
                job->valid_ranges = 0;
    }
    job->executable = marcus_preview_executable();
    job->thread = SDL_CreateThread(timing_thread, "marcus-timing", job);
    if (!job->thread)
    {
        *failure = "Cannot start the movie timing worker.";
        marcus_timing_worker_close(job);
        return NULL;
    }
    return job;
}

int marcus_timing_worker_take(MarcusTimingWorker *job, MMMovieSoundSchedule **out, const char **failure)
{
    if (!SDL_GetAtomicInt(&job->done))
        return 0;
    *out = job->result;
    job->result = NULL;
    *failure = job->failure;
    return 1;
}

void marcus_timing_worker_close(MarcusTimingWorker *job)
{
    if (!job)
        return;
    if (job->thread)
    {
        SDL_SetAtomicInt(&job->cancelled, 1);
        SDL_WaitThread(job->thread, NULL);
    }
    mm_movie_sound_schedule_destroy(job->result);
    SDL_free(job->executable);
    free(job->ranges);
    free(job);
}
