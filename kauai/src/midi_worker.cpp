/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_worker.h"
#include "core_platform.h"
#include <atomic>
#include <memory>
#include <thread>
#include <system_error>
namespace KauaiMidi
{
struct Worker::State
{
    KauaiPlatform::AutoResetEvent wake;
    std::atomic<bool> stop{false}, running{true};
    std::thread thread;
};
bool Worker::Start(Work work, void *user)
{
    if (state || !work)
        return false;
    try
    {
        std::unique_ptr<State> created(new State);
        auto shared = created.get();
        shared->thread = std::thread([=] {
            std::uint32_t wait = UINT32_MAX;
            for (;;)
            {
                shared->wake.Wait(wait);
                if (shared->stop.load())
                    return;
                wait = work(user);
            }
        });
        state = created.release();
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    catch (const std::system_error &)
    {
        return false;
    }
}
void Worker::Signal()
{
    if (state)
        state->wake.Signal();
}
bool Worker::Running() const
{
    return state && state->running.load();
}
void Worker::Stop()
{
    if (state && state->thread.joinable())
    {
        state->stop.store(true);
        state->wake.Signal();
        state->thread.join();
        state->running.store(false);
    }
}
Worker::~Worker()
{
    Stop();
    delete state;
}
} // namespace KauaiMidi
