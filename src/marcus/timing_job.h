#ifndef MARCUS_TIMING_JOB_H
#define MARCUS_TIMING_JOB_H
#include "movie_input.h"
#include "sound_timeline.h"
#include <iosfwd>

// Process management and host-only timing import run on this thread. All engine
// work belongs to the isolated helper, never a second in-process Preview.
class MarcusTimingJob
{
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    MarcusTimingJob(std::shared_ptr<MarcusMovieFile> movie, std::string resources, std::string content,
                    const MarcusScene *scenes, std::size_t count, int rate = 0, int modifier = 0);
    ~MarcusTimingJob();
    // Called once on the owner thread after completion. Failure is terminal.
    bool Take(std::unique_ptr<MarcusSoundTimeline> &timeline, std::string &error);
};
bool MarcusWriteTiming(MarcusPlayer *player, std::ostream &output);
#endif
