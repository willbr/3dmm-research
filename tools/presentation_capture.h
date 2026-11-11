/* Test-only bounded asynchronous RGB capture. Include before legacy headers. */
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace PlaybackCapture
{
struct Event
{
    int scene, frame;
    std::uint64_t nanoseconds;
    std::string path;
};
class Writer
{
    struct Job
    {
        std::string path;
        int width, height;
        std::vector<unsigned char> rgb;
    };
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Job> jobs;
    std::thread worker;
    bool stopping = false;
    std::string directory;
    std::vector<unsigned char> previous;
    std::string previousPath;

  public:
    std::vector<Event> events;
    std::atomic<bool> failed{false};
    bool active = false;
    ~Writer()
    {
        End();
    }
    void End()
    {
        active = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        ready.notify_one();
        if (worker.joinable())
            worker.join();
    }
    void Begin(const std::string &path)
    {
        End();
        directory = path;
        events.clear();
        previous.clear();
        previousPath.clear();
        failed = false;
        stopping = false;
        active = !path.empty();
        if (!active)
            return;
        worker = std::thread([this] {
            for (;;)
            {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    ready.wait(lock, [this] { return stopping || !jobs.empty(); });
                    if (jobs.empty())
                        break;
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                std::ofstream file(job.path, std::ios::binary);
                file << "P6\n" << job.width << ' ' << job.height << "\n255\n";
                file.write(reinterpret_cast<const char *>(job.rgb.data()), job.rgb.size());
                file.close();
                if (!file)
                    failed = true;
            }
        });
    }
    void Add(const unsigned char *pixels, int width, int height, int pitch, bool bgra, int scene, int frame,
             std::uint64_t ns)
    {
        if (!active || failed)
            return;
        std::lock_guard<std::mutex> lock(mutex);
        if (events.size() >= 20000)
        {
            failed = true;
            return;
        }
        Job job;
        job.width = width;
        job.height = height;
        job.path = directory + "/presentation-" + std::to_string(events.size()) + ".ppm";
        job.rgb.resize(std::size_t(width) * height * 3);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto source = pixels + y * pitch + x * 4;
                auto dest = job.rgb.data() + (std::size_t(y) * width + x) * 3;
                dest[0] = source[bgra ? 2 : 0];
                dest[1] = source[1];
                dest[2] = source[bgra ? 0 : 2];
            }
        if (job.rgb == previous)
        {
            events.push_back({scene, frame, ns, previousPath});
            return;
        }
        if (jobs.size() >= 128)
        {
            failed = true;
            return;
        }
        previous = job.rgb;
        previousPath = job.path;
        events.push_back({scene, frame, ns, job.path});
        jobs.push_back(std::move(job));
        ready.notify_one();
    }
};
} // namespace PlaybackCapture
