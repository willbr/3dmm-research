/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_worker.h"
#include "core_platform.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
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
    static MMU32 Run(void *user)
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
    using Worker = std::unique_ptr<MMMidiWorker, decltype(&mm_midi_worker_destroy)>;
    CHECK(!mm_midi_worker_running(nullptr) && !mm_midi_worker_start(nullptr, nullptr));
    mm_midi_worker_signal(nullptr);
    const Worker worker(mm_midi_worker_start(WorkState::Run, &state), mm_midi_worker_destroy);
    CHECK(worker && mm_midi_worker_running(worker.get()));
    mm_midi_worker_signal(worker.get());
    CHECK(state.called.Wait(2000));
    for (int i = 0; i < 200 && state.count < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(state.count == 2); // Timed wake.
    mm_midi_worker_signal(worker.get());
    CHECK(state.blocked.Wait(2000));
    mm_midi_worker_signal(worker.get());
    mm_midi_worker_signal(worker.get());
    mm_midi_worker_signal(worker.get()); // Coalesce while work runs.
    state.release.Signal();
    // The callback notification also coalesces, so observe the count directly.
    for (int i = 0; i < 200 && state.count != 4; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(state.count == 4);
    mm_midi_worker_stop(worker.get());
    CHECK(!mm_midi_worker_running(worker.get()));
    mm_midi_worker_signal(worker.get());
    mm_midi_worker_stop(worker.get());
    CHECK(state.count == 4);
    // Stop must join in-flight work, not just wake an idle thread.
    AutoResetEvent entered, leave;
    std::atomic<bool> finished{false};
    struct Gate
    {
        AutoResetEvent *entered, *leave;
        std::atomic<bool> *finished;
    } gate{&entered, &leave, &finished};
    const Worker blocked(mm_midi_worker_start(
        [](void *arg) -> MMU32 {
            auto &gate = *static_cast<Gate *>(arg);
            gate.entered->Signal();
            gate.leave->Wait();
            gate.finished->store(true);
            return UINT32_MAX;
        },
        &gate), mm_midi_worker_destroy);
    CHECK(blocked);
    mm_midi_worker_signal(blocked.get());
    CHECK(entered.Wait(2000));
    std::thread unblock([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        mm_midi_worker_signal(blocked.get());
        CHECK(mm_midi_worker_running(blocked.get()));
        leave.Signal();
    });
    mm_midi_worker_stop(blocked.get());
    CHECK(finished && !mm_midi_worker_running(blocked.get()));
    unblock.join();
    std::puts(
        "MIDI worker: auto-reset/coalescing, single-waiter wake, timer, pending signals and joined shutdown passed.");
}
