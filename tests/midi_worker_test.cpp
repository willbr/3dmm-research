/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_worker.h"
#include "core_platform.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "MIDI worker failed at %d\n", __LINE__);                                              \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
struct WorkState
{
    KauaiPlatform::AutoResetEvent called, blocked, release;
    std::atomic<int> count{0};
    static std::uint32_t Run(void *user)
    {
        auto &self = *static_cast<WorkState *>(user);
        const auto count = ++self.count;
        if (count == 3)
        {
            self.blocked.Signal();
            self.release.Wait();
        }
        self.called.Signal();
        return count == 1 ? 30 : UINT32_MAX;
    }
};
int main()
{
    using KauaiPlatform::AutoResetEvent;
    AutoResetEvent event;
    CHECK(!event.Wait(0));
    event.Signal();
    event.Signal();
    CHECK(event.Wait(0) && !event.Wait(0));
    std::atomic<int> ready{0}, signaled{0};
    auto waiter = [&] {
        ++ready;
        if (event.Wait(150))
            ++signaled;
    };
    std::thread first(waiter), second(waiter);
    while (ready != 2)
        std::this_thread::yield();
    event.Signal();
    first.join();
    second.join();
    CHECK(signaled == 1);
    WorkState state;
    KauaiMidi::Worker worker;
    CHECK(!worker.Running() && !worker.Start(nullptr, nullptr));
    worker.Signal();
    CHECK(worker.Start(WorkState::Run, &state) && worker.Running());
    CHECK(!worker.Start(WorkState::Run, &state));
    worker.Signal();
    CHECK(state.called.Wait(2000));
    for (int i = 0; i < 200 && state.count < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(state.count == 2); // Timed wake.
    worker.Signal();
    CHECK(state.blocked.Wait(2000));
    worker.Signal();
    worker.Signal();
    worker.Signal(); // Coalesce while work runs.
    state.release.Signal();
    // The callback notification also coalesces, so observe the count directly.
    for (int i = 0; i < 200 && state.count != 4; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(state.count == 4);
    worker.Stop();
    CHECK(!worker.Running());
    worker.Signal();
    worker.Stop();
    CHECK(state.count == 4);
    // Stop must join in-flight work, not just wake an idle thread.
    AutoResetEvent entered, leave;
    std::atomic<bool> finished{false};
    struct Gate
    {
        AutoResetEvent *entered, *leave;
        std::atomic<bool> *finished;
    } gate{&entered, &leave, &finished};
    KauaiMidi::Worker blocked;
    CHECK(blocked.Start(
        [](void *arg) -> std::uint32_t {
            auto &gate = *static_cast<Gate *>(arg);
            gate.entered->Signal();
            gate.leave->Wait();
            gate.finished->store(true);
            return UINT32_MAX;
        },
        &gate));
    blocked.Signal();
    CHECK(entered.Wait(2000));
    std::thread unblock([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        leave.Signal();
    });
    blocked.Stop();
    CHECK(finished && !blocked.Running());
    unblock.join();
    std::puts(
        "MIDI worker: auto-reset/coalescing, single-waiter wake, timer, pending signals and joined shutdown passed.");
}
