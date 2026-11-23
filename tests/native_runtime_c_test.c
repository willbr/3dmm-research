#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mm/native_runtime.h"
#include <assert.h>
#include <stdio.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <time.h>
#endif
typedef struct Shared {
    MMNativeRuntime *runtime;
    MMNativeMutex *mutex;
    MMNativeEvent *event;
    unsigned count, ready, released;
} Shared;
typedef struct Work { Shared *shared; MMU32 token; int wait; } Work;
static void run(void *context)
{
    Work *work = context;
    Shared *s;
    MMU32 repeated;
    unsigned i;
    int signaled;
    s = work->shared;
    assert(!mm_native_runtime_thread_token(s->runtime, &work->token));
    assert(!mm_native_runtime_thread_token(s->runtime, &repeated) && repeated == work->token);
    if (work->wait) {
        assert(!mm_native_mutex_enter(s->mutex)); ++s->ready; assert(!mm_native_mutex_leave(s->mutex));
        assert(!mm_native_event_wait(s->event, 150, &signaled));
        if (signaled) { assert(!mm_native_mutex_enter(s->mutex)); ++s->released; assert(!mm_native_mutex_leave(s->mutex)); }
    } else for (i = 0; i < 1000; ++i) {
        assert(!mm_native_mutex_enter(s->mutex) && !mm_native_mutex_enter(s->mutex));
        ++s->count;
        assert(!mm_native_mutex_leave(s->mutex) && !mm_native_mutex_leave(s->mutex));
    }
}
#ifdef _WIN32
static void pause_thread(void) { Sleep(1); }
#else
static void pause_thread(void) { struct timespec delay; delay.tv_sec = 0; delay.tv_nsec = 1000000; nanosleep(&delay, NULL); }
#endif
typedef struct SelfJoin
{
    MMNativeThread *thread;
    MMNativeEvent *published, *done;
    int status;
} SelfJoin;
static void reject_self_join(void *context)
{
    SelfJoin *job = context;
    int signaled;
    assert(!mm_native_event_wait(job->published, 2000, &signaled) && signaled);
    job->status = mm_native_thread_join(job->thread);
    assert(!mm_native_event_signal(job->done));
}
static MMU32 ticks, calls;
static MMNativeRuntime *reentrant;
static MMU32 fake(void) { ++calls; return ticks; }
static MMU32 other(void) { return 17; }
static MMU32 reset(void)
{
    MMNativeTickSource previous;
    assert(!mm_native_runtime_exchange_ticks(reentrant, NULL, &previous) && previous == reset);
    return 42;
}
static int reject(void *context, MMNativeTickSource source, MMU32 *value)
{ assert(context == &ticks && source == fake); *value = 123; return 77; }
int main(void)
{
    Shared s;
    Work work[4];
    MMNativeThread *threads[4];
    SelfJoin self;
    MMNativeRuntime *second;
    MMNativeTickSource previous;
    MMU32 token, value, before, after;
    unsigned i, j, ready;
    int error, signaled;
    s.runtime = mm_native_runtime_create(&error); assert(s.runtime && !error);
    s.mutex = mm_native_mutex_create(&error); assert(s.mutex && !error);
    s.event = mm_native_event_create(&error); assert(s.event && !error);
    s.count = s.ready = s.released = 0;
    assert(!mm_native_runtime_thread_token(s.runtime, &token) && token == 1);
    assert(!mm_native_event_wait(s.event, 0, &signaled) && !signaled);
    assert(!mm_native_event_signal(s.event) && !mm_native_event_signal(s.event));
    assert(!mm_native_event_wait(s.event, 0, &signaled) && signaled);
    assert(!mm_native_event_wait(s.event, 0, &signaled) && !signaled);
    assert(!mm_native_runtime_ticks(s.runtime, &before, NULL, NULL));
    assert(!mm_native_event_wait(s.event, 10, &signaled) && !signaled);
    assert(!mm_native_runtime_ticks(s.runtime, &after, NULL, NULL) && (MMU32)(after - before) >= 9);
    for (i = 0; i < 4; ++i) {
        work[i].shared = &s; work[i].wait = 0;
        threads[i] = mm_native_thread_start(run, &work[i], &error);
        assert(threads[i] && !error);
    }
    for (i = 0; i < 4; ++i) assert(!mm_native_thread_join(threads[i]));
    assert(s.count == 4000);
    for (i = 0; i < 4; ++i) {
        assert(work[i].token != token);
        for (j = 0; j < i; ++j) assert(work[i].token != work[j].token);
    }
    for (i = 0; i < 2; ++i) {
        work[i].wait = 1;
        threads[i] = mm_native_thread_start(run, &work[i], &error);
        assert(threads[i] && !error);
    }
    ready = 0;
    for (i = 0; i < 3000 && ready != 2; ++i) {
        assert(!mm_native_mutex_enter(s.mutex)); ready = s.ready; assert(!mm_native_mutex_leave(s.mutex));
        if (ready != 2) pause_thread();
    }
    assert(ready == 2 && !mm_native_event_signal(s.event));
    assert(!mm_native_thread_join(threads[0]) && !mm_native_thread_join(threads[1]) && s.released == 1);
    self.published = mm_native_event_create(NULL);
    self.done = mm_native_event_create(NULL);
    assert(self.published && self.done);
    self.thread = mm_native_thread_start(reject_self_join, &self, &error);
    assert(self.thread && !error && !mm_native_event_signal(self.published));
    assert(!mm_native_event_wait(self.done, 2000, &signaled) && signaled);
    assert(self.status && !mm_native_thread_join(self.thread));
    mm_native_event_destroy(self.published); mm_native_event_destroy(self.done);
    second = mm_native_runtime_create(&error); assert(second && !error);
    assert(!mm_native_runtime_thread_token(second, &value) && value == 1);
    assert(!mm_native_runtime_thread_token(s.runtime, &value) && value == token);
    assert(!mm_native_runtime_exchange_ticks(s.runtime, fake, &previous) && !previous);
    assert(!mm_native_runtime_exchange_ticks(second, other, &previous) && !previous);
    ticks = 0xfffffff0UL;
    assert(!mm_native_runtime_ticks(s.runtime, &before, NULL, NULL) && before == ticks);
    ticks = 16;
    assert(!mm_native_runtime_ticks(s.runtime, &after, NULL, NULL) && (MMU32)(after - before) == 32);
    assert(!mm_native_runtime_ticks(second, &value, NULL, NULL) && value == 17);
    value = 99; before = calls;
    assert(mm_native_runtime_ticks(s.runtime, &value, &ticks, reject) == 77 && value == 99 && before == calls);
    reentrant = s.runtime;
    assert(!mm_native_runtime_exchange_ticks(s.runtime, reset, &previous) && previous == fake);
    assert(!mm_native_runtime_ticks(s.runtime, &value, NULL, NULL) && value == 42);
    assert(!mm_native_runtime_exchange_ticks(s.runtime, NULL, &previous) && !previous);
    mm_native_runtime_destroy(second);
    mm_native_event_destroy(s.event); mm_native_mutex_destroy(s.mutex); mm_native_runtime_destroy(s.runtime);
    mm_native_event_destroy(NULL); mm_native_mutex_destroy(NULL); mm_native_runtime_destroy(NULL);
    puts("C-only runtime: recursive locks, event coalescing/deadlines, thread tokens, contexts and clock callbacks passed.");
    return 0;
}
