#ifndef MARCUS_FRAME_PREVIEWS_H
#define MARCUS_FRAME_PREVIEWS_H
#include "movie_input.h"
#include "browser_preview.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// Owns only process/IPC state. Engine evaluation runs in marcus-preview.
struct MarcusFramePreviews
{
    struct Result { MarcusFramePosition position; BrowserPreview preview; };
    std::atomic<bool> cancel{false};
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<MarcusFramePosition> pending;
    std::deque<Result> ready;
    bool busy = false;
    MarcusFramePosition active = {};
    int sourceScene = 0;
    std::string failure;
    Result output;
    std::thread thread;
    MarcusFramePreviews(std::shared_ptr<MarcusMovieFile> movie, std::string resources, std::string content, int scene);
    ~MarcusFramePreviews();
};
#endif
