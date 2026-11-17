/* Native failure paths, spurious wakeups and initialized zero-token checks. */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define NOMINMAX
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif
#include "mm/native_runtime.h"
#include <assert.h>
#include <stdlib.h>
static int allocation_fail, allocations, keys, key_calls, key_fail, locks, lock_calls, lock_fail;
static int clock_fail, clock_fake, clock_calls, spurious, wait_calls;
static void *allocate(size_t bytes)
{
    void *value;
    if (allocation_fail) return NULL;
    value = malloc(bytes); if (value) ++allocations;
    return value;
}
static void release(void *value) { if (value) --allocations; free(value); }
#ifdef _WIN32
static BOOL initialize(CRITICAL_SECTION *lock, DWORD spin, DWORD flags)
{
    BOOL ok;
    ++lock_calls;
    if (lock_calls == lock_fail) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    ok = InitializeCriticalSectionEx(lock, spin, flags); if (ok) ++locks;
    return ok;
}
static void destroy_lock(CRITICAL_SECTION *lock) { --locks; DeleteCriticalSection(lock); }
static DWORD create_key(void)
{
    DWORD key;
    ++key_calls;
    if (key_calls == key_fail) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return TLS_OUT_OF_INDEXES; }
    key = TlsAlloc(); if (key != TLS_OUT_OF_INDEXES) ++keys;
    return key;
}
static BOOL delete_key(DWORD key) { --keys; return TlsFree(key); }
static BOOL now_counter(LARGE_INTEGER *value)
{
    if (clock_fail) { SetLastError(ERROR_GEN_FAILURE); return FALSE; }
    if (clock_fake) {
        if (clock_fake >= 3) {
            value->LowPart = clock_fake == 3 ? 0xffffd8f0UL : 0xffffd8f1UL;
            value->HighPart = -1; return TRUE;
        }
        value->LowPart = clock_fake == 2 && clock_calls == 0 ? 0 : 100000;
        value->HighPart = clock_fake == 2 && clock_calls == 0 ? 0 : 10000;
        ++clock_calls; return TRUE;
    }
    return QueryPerformanceCounter(value);
}
static BOOL wait_changed(CONDITION_VARIABLE *changed, CRITICAL_SECTION *lock, DWORD timeout)
{
    ++wait_calls;
    if (spurious) { --spurious; return TRUE; }
    return SleepConditionVariableCS(changed, lock, timeout);
}
#define malloc allocate
#define free release
#define InitializeCriticalSectionEx initialize
#define DeleteCriticalSection destroy_lock
#define TlsAlloc create_key
#define TlsFree delete_key
#define QueryPerformanceCounter now_counter
#define SleepConditionVariableCS wait_changed
#include "../libraries/engine/src/native_runtime_win.c"
#undef malloc
#undef free
#undef InitializeCriticalSectionEx
#undef DeleteCriticalSection
#undef TlsAlloc
#undef TlsFree
#undef QueryPerformanceCounter
#undef SleepConditionVariableCS
static DWORD WINAPI zero_token(void *opaque)
{
    MMU32 value;
    assert(!mm_native_runtime_thread_token((MMNativeRuntime *)opaque, &value) && value == 0);
    assert(!mm_native_runtime_thread_token((MMNativeRuntime *)opaque, &value) && value == 0);
    return 0;
}
#else
static int initialize(pthread_mutex_t *lock, const pthread_mutexattr_t *attributes)
{
    int status;
    ++lock_calls;
    if (lock_calls == lock_fail) return EAGAIN;
    status = pthread_mutex_init(lock, attributes); if (!status) ++locks;
    return status;
}
static int destroy_lock(pthread_mutex_t *lock) { --locks; return pthread_mutex_destroy(lock); }
static int create_key(pthread_key_t *key, void (*destructor)(void *))
{
    int status;
    ++key_calls;
    if (key_calls == key_fail) return EAGAIN;
    status = pthread_key_create(key, destructor); if (!status) ++keys;
    return status;
}
static int delete_key(pthread_key_t key) { --keys; return pthread_key_delete(key); }
static int now_counter(clockid_t clock, struct timespec *value)
{
    if (clock_fail) { errno = EIO; return -1; }
    if (clock_fake) {
        if (clock_fake >= 3) {
            value->tv_sec = -1; value->tv_nsec = clock_fake == 3 ? 999000000 : 999000001; return 0;
        }
        value->tv_sec = clock_fake == 2 && clock_calls == 0 ? 0 : 4294967;
        value->tv_nsec = clock_fake == 2 && clock_calls == 0 ? 0 : 306000000;
        ++clock_calls; return 0;
    }
    return clock_gettime(clock, value);
}
static int wait_changed(pthread_cond_t *changed, pthread_mutex_t *lock, const struct timespec *timeout)
{
    ++wait_calls;
    if (spurious) { --spurious; return 0; }
#ifdef __APPLE__
    return pthread_cond_timedwait_relative_np(changed, lock, timeout);
#else
    return pthread_cond_timedwait(changed, lock, timeout);
#endif
}
#define malloc allocate
#define free release
#define pthread_mutex_init initialize
#define pthread_mutex_destroy destroy_lock
#define pthread_key_create create_key
#define pthread_key_delete delete_key
#define clock_gettime now_counter
#ifdef __APPLE__
#define pthread_cond_timedwait_relative_np wait_changed
#else
#define pthread_cond_timedwait wait_changed
#endif
#include "../libraries/engine/src/native_runtime_posix.c"
#undef malloc
#undef free
#undef pthread_mutex_init
#undef pthread_mutex_destroy
#undef pthread_key_create
#undef pthread_key_delete
#undef clock_gettime
#ifdef __APPLE__
#undef pthread_cond_timedwait_relative_np
#else
#undef pthread_cond_timedwait
#endif
static void *zero_token(void *opaque)
{
    MMU32 value;
    assert(!mm_native_runtime_thread_token((MMNativeRuntime *)opaque, &value) && value == 0);
    assert(!mm_native_runtime_thread_token((MMNativeRuntime *)opaque, &value) && value == 0);
    return NULL;
}
#endif
int main(void)
{
    MMNativeRuntime *runtime;
    MMNativeEvent *event;
    MMNativeMutex *mutex;
    MMU32 value, before, after;
    int error, signaled;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
    allocation_fail = 1;
    assert(!mm_native_runtime_create(&error) && error);
    assert(!mm_native_mutex_create(&error) && error);
    assert(!mm_native_event_create(&error) && error);
    assert(!allocations && !keys && !locks);
    allocation_fail = 0; lock_calls = 0; lock_fail = 1;
    assert(!mm_native_mutex_create(&error) && error && !allocations && !locks);
    lock_calls = 0;
    assert(!mm_native_runtime_create(&error) && error && !allocations && !locks);
    lock_calls = 0;
    assert(!mm_native_event_create(&error) && error && !allocations && !locks);
    lock_fail = 0;
    for (key_fail = 1; key_fail <= 2; ++key_fail) {
        key_calls = 0;
        assert(!mm_native_runtime_create(&error) && error && !allocations && !keys && !locks);
    }
    key_fail = 0;
    runtime = mm_native_runtime_create(&error); assert(runtime && !error && keys == 2);
    event = mm_native_event_create(&error); assert(event && !error);
    mutex = mm_native_mutex_create(&error); assert(mutex && !error);
    value = 77; signaled = 88; clock_fail = 1;
    assert(mm_native_runtime_ticks(runtime, &value, NULL, NULL) && value == 77);
    assert(mm_native_event_wait(event, 1, &signaled) && signaled == 88);
    clock_fail = 0;
    assert(!mm_native_event_signal(event));
    assert(!mm_native_event_wait(event, 0, &signaled) && signaled);
    assert(!mm_native_runtime_ticks(runtime, &before, NULL, NULL));
    spurious = 3;
    assert(!mm_native_event_wait(event, 10, &signaled) && !signaled && wait_calls >= 4);
    assert(!mm_native_runtime_ticks(runtime, &after, NULL, NULL) && (MMU32)(after - before) >= 9);
    clock_fake = 1; clock_calls = 0;
#ifdef _WIN32
    runtime->frequency.low = event->frequency.low = 10000000;
    runtime->frequency.high = event->frequency.high = 0;
#endif
    assert(!mm_native_runtime_ticks(runtime, &value, NULL, NULL) && value == 10);
    clock_fake = 2; clock_calls = 0; wait_calls = 0;
    assert(!mm_native_event_wait(event, 0xfffffffeUL, &signaled) && !signaled && !wait_calls);
    clock_fake = 3;
    assert(!mm_native_runtime_ticks(runtime, &value, NULL, NULL) && value == 0xffffffffUL);
    clock_fake = 4;
    assert(!mm_native_runtime_ticks(runtime, &value, NULL, NULL) && value == 0);
    clock_fake = 0;
    runtime->next_token = 0xffffffffUL;
#ifdef _WIN32
    SetLastError(12345);
#else
    errno = 12345;
#endif
    assert(!mm_native_runtime_thread_token(runtime, &value) && value == 0xffffffffUL);
#ifdef _WIN32
    assert(GetLastError() == 12345);
#else
    assert(errno == 12345);
#endif
    assert(!mm_native_runtime_thread_token(runtime, &value) && value == 0xffffffffUL);
#ifdef _WIN32
    assert(GetLastError() == 12345);
#else
    assert(errno == 12345);
#endif
#ifdef _WIN32
    thread = CreateThread(NULL, 0, zero_token, runtime, 0, NULL); assert(thread);
    assert(WaitForSingleObject(thread, 3000) == WAIT_OBJECT_0); CloseHandle(thread);
#else
    assert(!pthread_create(&thread, NULL, zero_token, runtime)); assert(!pthread_join(thread, NULL));
#endif
    assert(!mm_native_runtime_thread_token(runtime, &value) && value == 0xffffffffUL && runtime->next_token == 1);
    mm_native_mutex_destroy(mutex); mm_native_event_destroy(event); mm_native_runtime_destroy(runtime);
    assert(!allocations && !locks && !keys);
    return 0;
}
