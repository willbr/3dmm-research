/* Core services converted from Kauai core_platform.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mm/native_runtime.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct MMNativeMutex { pthread_mutex_t lock; };
struct MMNativeEvent { pthread_mutex_t lock; pthread_cond_t changed; int signaled; };
struct MMNativeRuntime {
    pthread_mutex_t lock;
    pthread_key_t token_key, initialized_key;
    MMU32 next_token;
    MMNativeTickSource source;
};
static int steady_now(struct timespec *value)
{
#ifdef __APPLE__
    return clock_gettime(CLOCK_MONOTONIC_RAW, value) == 0 ? 0 : errno;
#else
    return clock_gettime(CLOCK_MONOTONIC, value) == 0 ? 0 : errno;
#endif
}
MMNativeMutex *mm_native_mutex_create(int *error)
{
    MMNativeMutex *mutex;
    pthread_mutexattr_t attributes;
    int status;
    mutex = (MMNativeMutex *)malloc(sizeof(*mutex));
    status = mutex ? pthread_mutexattr_init(&attributes) : ENOMEM;
    if (mutex && !status) {
        status = pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
        if (!status) status = pthread_mutex_init(&mutex->lock, &attributes);
        pthread_mutexattr_destroy(&attributes);
    }
    if (status) { free(mutex); mutex = NULL; }
    if (error) *error = status;
    return mutex;
}
void mm_native_mutex_destroy(MMNativeMutex *mutex)
{ if (mutex) { pthread_mutex_destroy(&mutex->lock); free(mutex); } }
int mm_native_mutex_enter(MMNativeMutex *mutex) { return pthread_mutex_lock(&mutex->lock); }
int mm_native_mutex_leave(MMNativeMutex *mutex) { return pthread_mutex_unlock(&mutex->lock); }
MMNativeEvent *mm_native_event_create(int *error)
{
    MMNativeEvent *event;
    pthread_condattr_t attributes;
    int status;
    event = (MMNativeEvent *)malloc(sizeof(*event));
    status = event ? pthread_mutex_init(&event->lock, NULL) : ENOMEM;
    if (event && !status) {
        status = pthread_condattr_init(&attributes);
        if (!status) {
#ifndef __APPLE__
            status = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
#endif
            if (!status) status = pthread_cond_init(&event->changed, &attributes);
            pthread_condattr_destroy(&attributes);
        }
        if (status) pthread_mutex_destroy(&event->lock);
        else event->signaled = 0;
    }
    if (status) { free(event); event = NULL; }
    if (error) *error = status;
    return event;
}
void mm_native_event_destroy(MMNativeEvent *event)
{
    if (!event) return;
    pthread_cond_destroy(&event->changed); pthread_mutex_destroy(&event->lock); free(event);
}
int mm_native_event_signal(MMNativeEvent *event)
{
    int status, unlock;
    status = pthread_mutex_lock(&event->lock);
    if (status) return status;
    event->signaled = 1;
    status = pthread_cond_signal(&event->changed);
    unlock = pthread_mutex_unlock(&event->lock);
    return status ? status : unlock;
}
int mm_native_event_wait(MMNativeEvent *event, MMU32 milliseconds, int *signaled)
{
    struct timespec deadline, now;
#ifdef __APPLE__
    struct timespec remaining;
#endif
    int status, unlock, finite, consumed;
    MMU32 seconds;
    status = pthread_mutex_lock(&event->lock);
    if (status) return status;
    finite = milliseconds != 0xffffffffUL; consumed = 0;
    if (!event->signaled && finite && milliseconds) {
        status = steady_now(&deadline);
        if (status) goto done;
        seconds = milliseconds / 1000;
        deadline.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) { ++seconds; deadline.tv_nsec -= 1000000000L; }
        if (sizeof(time_t) <= 4 && deadline.tv_sec >= 0 && (MMU32)deadline.tv_sec > 0x7fffffffUL - seconds) {
            status = EOVERFLOW; goto done;
        }
        deadline.tv_sec += (time_t)seconds;
    }
    while (!event->signaled) {
        if (!finite) status = pthread_cond_wait(&event->changed, &event->lock);
        else {
            if (!milliseconds) break;
            status = steady_now(&now);
            if (status) goto done;
            if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) break;
#ifdef __APPLE__
            remaining.tv_sec = deadline.tv_sec - now.tv_sec;
            remaining.tv_nsec = deadline.tv_nsec - now.tv_nsec;
            if (remaining.tv_nsec < 0) { --remaining.tv_sec; remaining.tv_nsec += 1000000000L; }
            status = pthread_cond_timedwait_relative_np(&event->changed, &event->lock, &remaining);
#else
            status = pthread_cond_timedwait(&event->changed, &event->lock, &deadline);
#endif
        }
        if (status && status != ETIMEDOUT) goto done;
        status = 0;
    }
    if (event->signaled) { event->signaled = 0; consumed = 1; }
done:
    unlock = pthread_mutex_unlock(&event->lock);
    if (!status) status = unlock;
    if (!status) *signaled = consumed;
    return status;
}
MMNativeRuntime *mm_native_runtime_create(int *error)
{
    MMNativeRuntime *runtime;
    int status;
    runtime = (MMNativeRuntime *)malloc(sizeof(*runtime));
    status = runtime ? pthread_mutex_init(&runtime->lock, NULL) : ENOMEM;
    if (runtime && !status) {
        status = pthread_key_create(&runtime->token_key, NULL);
        if (!status) {
            status = pthread_key_create(&runtime->initialized_key, NULL);
            if (status) pthread_key_delete(runtime->token_key);
        }
        if (status) pthread_mutex_destroy(&runtime->lock);
        else { runtime->next_token = 1; runtime->source = NULL; }
    }
    if (status) { free(runtime); runtime = NULL; }
    if (error) *error = status;
    return runtime;
}
void mm_native_runtime_destroy(MMNativeRuntime *runtime)
{
    if (!runtime) return;
    pthread_key_delete(runtime->initialized_key); pthread_key_delete(runtime->token_key);
    pthread_mutex_destroy(&runtime->lock); free(runtime);
}
int mm_native_runtime_thread_token(MMNativeRuntime *runtime, MMU32 *token)
{
    MMU32 value;
    int status, saved_errno;
    saved_errno = errno; status = 0;
    if (pthread_getspecific(runtime->initialized_key)) {
        value = (MMU32)(size_t)pthread_getspecific(runtime->token_key);
        goto done;
    }
    status = pthread_mutex_lock(&runtime->lock);
    if (status) goto done;
    value = runtime->next_token++;
    status = pthread_mutex_unlock(&runtime->lock);
    if (status) goto done;
    status = pthread_setspecific(runtime->token_key, (void *)(size_t)value);
    if (!status) status = pthread_setspecific(runtime->initialized_key, (void *)1);
done:
    errno = saved_errno;
    if (!status) *token = value;
    return status;
}
int mm_native_runtime_exchange_ticks(MMNativeRuntime *runtime, MMNativeTickSource source, MMNativeTickSource *previous)
{
    MMNativeTickSource old;
    int status;
    status = pthread_mutex_lock(&runtime->lock);
    if (status) return status;
    old = runtime->source; runtime->source = source;
    status = pthread_mutex_unlock(&runtime->lock);
    if (!status && previous) *previous = old;
    return status;
}
int mm_native_runtime_ticks(MMNativeRuntime *runtime, MMU32 *ticks, void *context, MMNativeTickInvoke invoke)
{
    MMNativeTickSource source;
    struct timespec now;
    MMU32 value;
    int status;
    status = pthread_mutex_lock(&runtime->lock);
    if (status) return status;
    source = runtime->source;
    status = pthread_mutex_unlock(&runtime->lock);
    if (status) return status;
    if (source) {
        if (invoke) { status = invoke(context, source, &value); if (status) return status; }
        else value = source();
    } else {
        status = steady_now(&now);
        if (status) return status;
        value = (MMU32)now.tv_sec * 1000 + (MMU32)(now.tv_nsec / 1000000L);
        if (now.tv_sec < 0 && now.tv_nsec % 1000000L) ++value;
    }
    *ticks = value;
    return 0;
}
void mm_native_break_into_debugger(void)
{
#if defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    abort();
#endif
}
