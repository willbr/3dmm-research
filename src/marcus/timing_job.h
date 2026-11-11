#ifndef MARCUS_TIMING_JOB_H
#define MARCUS_TIMING_JOB_H
#include "movie_input.h"
#include "sound_timeline.h"
#include <atomic>
#include <thread>
#include <vector>
#include <iosfwd>

// Process management and host-only timing import run on this thread. All engine
// work belongs to the isolated helper, never a second in-process Preview.
class MarcusTimingJob
{
    std::atomic<bool> cancel{false}, done{false};
    std::thread thread;
    std::unique_ptr<MarcusSoundTimeline> result;
    std::string failure;

  public:
    MarcusTimingJob(std::shared_ptr<MarcusMovieFile> movie, std::string resources, std::string content,
                    std::vector<MarcusScene> scenes);
    ~MarcusTimingJob();
    // Called once on the owner thread after completion. Failure is terminal.
    bool Take(std::unique_ptr<MarcusSoundTimeline> &timeline, std::string &error);
};
bool MarcusWriteTiming(MarcusPlayer *player, std::ostream &output);
#endif
