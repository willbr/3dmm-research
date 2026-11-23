/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "midi_worker.h"
#include "mm/native_runtime.h"
#include <stdlib.h>

struct MMMidiWorker
{
    MMNativeThread *thread;
    MMNativeEvent *wake;
    MMNativeMutex *lock;
    MMMidiWork work;
    void *context;
    int stopping, running;
};

static void lock_state(MMMidiWorker *worker)
{
    if (mm_native_mutex_enter(worker->lock))
        abort();
}

static void unlock_state(MMMidiWorker *worker)
{
    if (mm_native_mutex_leave(worker->lock))
        abort();
}

static void run_worker(void *context)
{
    MMMidiWorker *worker = context;
    MMU32 wait = 0xffffffffUL;
    int signaled, stopping;
    while (!mm_native_event_wait(worker->wake, wait, &signaled))
    {
        lock_state(worker);
        stopping = worker->stopping;
        unlock_state(worker);
        if (stopping)
            return;
        wait = worker->work(worker->context);
    }
    lock_state(worker);
    worker->running = 0;
    unlock_state(worker);
}

MMMidiWorker *mm_midi_worker_start(MMMidiWork work, void *context)
{
    MMMidiWorker *worker;
    if (!work)
        return NULL;
    worker = calloc(1, sizeof(*worker));
    if (!worker)
        return NULL;
    worker->lock = mm_native_mutex_create(NULL);
    if (worker->lock)
        worker->wake = mm_native_event_create(NULL);
    if (worker->wake)
    {
        worker->work = work;
        worker->context = context;
        worker->running = 1;
        worker->thread = mm_native_thread_start(run_worker, worker, NULL);
        if (worker->thread)
            return worker;
    }
    mm_midi_worker_destroy(worker);
    return NULL;
}

void mm_midi_worker_signal(MMMidiWorker *worker)
{
    if (worker && mm_native_event_signal(worker->wake))
        abort();
}

int mm_midi_worker_running(MMMidiWorker *worker)
{
    int running;
    if (!worker)
        return 0;
    lock_state(worker);
    running = worker->running;
    unlock_state(worker);
    return running;
}

void mm_midi_worker_stop(MMMidiWorker *worker)
{
    if (!worker || !worker->thread)
        return;
    lock_state(worker);
    worker->stopping = 1;
    unlock_state(worker);
    mm_midi_worker_signal(worker);
    if (mm_native_thread_join(worker->thread))
        abort();
    worker->thread = NULL;
    lock_state(worker);
    worker->running = 0;
    unlock_state(worker);
}

void mm_midi_worker_destroy(MMMidiWorker *worker)
{
    if (worker)
    {
        mm_midi_worker_stop(worker);
        mm_native_event_destroy(worker->wake);
        mm_native_mutex_destroy(worker->lock);
        free(worker);
    }
}
