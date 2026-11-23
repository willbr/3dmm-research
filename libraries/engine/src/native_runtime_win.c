/* Core services converted from Kauai core_platform.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define NOMINMAX
#include "mm/native_runtime.h"
#include "mm/runtime_math.h"
#include <errno.h>
#include <process.h>
#include <stdlib.h>
#include <windows.h>

struct MMNativeMutex { CRITICAL_SECTION lock; };
struct MMNativeThread {
    HANDLE handle;
    unsigned id;
    MMNativeThreadEntry entry;
    void *context;
};
struct MMNativeEvent {
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE changed;
    int signaled;
    MMRuntimeCounter frequency;
};
struct MMNativeRuntime {
    CRITICAL_SECTION lock;
    DWORD token_key, initialized_key;
    MMU32 next_token;
    MMNativeTickSource source;
    MMRuntimeCounter frequency;
};
static int native_error(void)
{ DWORD error = GetLastError(); return error ? (int)error : ERROR_GEN_FAILURE; }
static int counter(MMRuntimeCounter *value)
{
    LARGE_INTEGER native;
    if (!QueryPerformanceCounter(&native)) return native_error();
    value->low = native.LowPart; value->high = (MMU32)native.HighPart;
    return 0;
}
static int frequency(MMRuntimeCounter *value)
{
    LARGE_INTEGER native;
    if (!QueryPerformanceFrequency(&native)) return native_error();
    if (native.HighPart < 0 || (!native.LowPart && !native.HighPart)) return ERROR_GEN_FAILURE;
    value->low = native.LowPart; value->high = (MMU32)native.HighPart;
    return 0;
}
MMNativeMutex *mm_native_mutex_create(int *error)
{
    MMNativeMutex *mutex;
    int status;
    mutex = (MMNativeMutex *)malloc(sizeof(*mutex));
    status = mutex ? 0 : ERROR_NOT_ENOUGH_MEMORY;
    if (mutex && !InitializeCriticalSectionEx(&mutex->lock, 0, 0)) {
        status = native_error(); free(mutex); mutex = NULL;
    }
    if (error) *error = status;
    return mutex;
}
void mm_native_mutex_destroy(MMNativeMutex *mutex)
{ if (mutex) { DeleteCriticalSection(&mutex->lock); free(mutex); } }
int mm_native_mutex_enter(MMNativeMutex *mutex) { EnterCriticalSection(&mutex->lock); return 0; }
int mm_native_mutex_leave(MMNativeMutex *mutex) { LeaveCriticalSection(&mutex->lock); return 0; }
MMNativeEvent *mm_native_event_create(int *error)
{
    MMNativeEvent *event;
    int status;
    event = (MMNativeEvent *)malloc(sizeof(*event));
    status = event ? 0 : ERROR_NOT_ENOUGH_MEMORY;
    if (event) {
        status = frequency(&event->frequency);
        if (!status && !InitializeCriticalSectionEx(&event->lock, 0, 0)) status = native_error();
        if (status) { free(event); event = NULL; }
        else { InitializeConditionVariable(&event->changed); event->signaled = 0; }
    }
    if (error) *error = status;
    return event;
}
void mm_native_event_destroy(MMNativeEvent *event)
{ if (event) { DeleteCriticalSection(&event->lock); free(event); } }
int mm_native_event_signal(MMNativeEvent *event)
{
    EnterCriticalSection(&event->lock);
    event->signaled = 1;
    WakeConditionVariable(&event->changed);
    LeaveCriticalSection(&event->lock);
    return 0;
}
int mm_native_event_wait(MMNativeEvent *event, MMU32 milliseconds, int *signaled)
{
    MMRuntimeCounter start, now, elapsed;
    DWORD remaining, error;
    int status, finite, consumed;
    status = 0; consumed = 0; finite = milliseconds != 0xffffffffUL;
    EnterCriticalSection(&event->lock);
    if (!event->signaled && finite && milliseconds) {
        status = counter(&start);
        if (status) goto done;
    }
    while (!event->signaled) {
        remaining = INFINITE;
        if (finite) {
            if (!milliseconds) break;
            status = counter(&now);
            if (status) goto done;
            elapsed = mm_runtime_counter_subtract(now, start);
            if (!mm_runtime_counter_scale(elapsed, 1000, event->frequency, &elapsed) ||
                elapsed.high || elapsed.low >= milliseconds) break;
            remaining = milliseconds - elapsed.low;
        }
        if (!SleepConditionVariableCS(&event->changed, &event->lock, remaining)) {
            error = GetLastError();
            if (error != ERROR_TIMEOUT) { status = error ? (int)error : ERROR_GEN_FAILURE; goto done; }
        }
    }
    if (event->signaled) { event->signaled = 0; consumed = 1; }
done:
    LeaveCriticalSection(&event->lock);
    if (!status) *signaled = consumed;
    return status;
}
static unsigned __stdcall run_thread(void *context)
{
    MMNativeThread *thread = context;
    thread->entry(thread->context);
    return 0;
}
MMNativeThread *mm_native_thread_start(MMNativeThreadEntry entry, void *context, int *error)
{
    MMNativeThread *thread = NULL;
    int status = EINVAL;
    if (entry) {
        thread = malloc(sizeof(*thread));
        status = thread ? 0 : ENOMEM;
    }
    if (thread) {
        thread->entry = entry;
        thread->context = context;
        thread->handle = (HANDLE)_beginthreadex(NULL, 0, run_thread, thread, 0, &thread->id);
        if (!thread->handle) {
            status = errno ? errno : EAGAIN;
            free(thread); thread = NULL;
        }
    }
    if (error) *error = status;
    return thread;
}
int mm_native_thread_join(MMNativeThread *thread)
{
    DWORD result;
    if (!thread) return EINVAL;
    /* A terminated thread's id may already have been recycled. */
    if (GetCurrentThreadId() == thread->id && WaitForSingleObject(thread->handle, 0) == WAIT_TIMEOUT)
        return EDEADLK;
    result = WaitForSingleObject(thread->handle, INFINITE);
    if (result != WAIT_OBJECT_0) return native_error();
    if (!CloseHandle(thread->handle)) return native_error();
    free(thread);
    return 0;
}
MMNativeRuntime *mm_native_runtime_create(int *error)
{
    MMNativeRuntime *runtime;
    int status;
    runtime = (MMNativeRuntime *)malloc(sizeof(*runtime));
    status = runtime ? 0 : ERROR_NOT_ENOUGH_MEMORY;
    if (runtime) {
        status = frequency(&runtime->frequency);
        if (status || !InitializeCriticalSectionEx(&runtime->lock, 0, 0)) {
            if (!status) status = native_error();
            free(runtime); runtime = NULL;
        } else {
            runtime->token_key = TlsAlloc();
            runtime->initialized_key = runtime->token_key == TLS_OUT_OF_INDEXES ? TLS_OUT_OF_INDEXES : TlsAlloc();
            if (runtime->initialized_key == TLS_OUT_OF_INDEXES) {
                status = native_error();
                if (runtime->token_key != TLS_OUT_OF_INDEXES) TlsFree(runtime->token_key);
                DeleteCriticalSection(&runtime->lock); free(runtime); runtime = NULL;
            } else { runtime->next_token = 1; runtime->source = NULL; }
        }
    }
    if (error) *error = status;
    return runtime;
}
void mm_native_runtime_destroy(MMNativeRuntime *runtime)
{
    if (!runtime) return;
    TlsFree(runtime->initialized_key); TlsFree(runtime->token_key);
    DeleteCriticalSection(&runtime->lock); free(runtime);
}
int mm_native_runtime_thread_token(MMNativeRuntime *runtime, MMU32 *token)
{
    void *initialized, *stored;
    MMU32 value;
    DWORD saved_error;
    int status;
    saved_error = GetLastError(); status = 0;
    SetLastError(ERROR_SUCCESS); initialized = TlsGetValue(runtime->initialized_key);
    if (!initialized && GetLastError() != ERROR_SUCCESS) { status = native_error(); goto done; }
    if (initialized) {
        SetLastError(ERROR_SUCCESS); stored = TlsGetValue(runtime->token_key);
        if (!stored && GetLastError() != ERROR_SUCCESS) { status = native_error(); goto done; }
        value = (MMU32)(size_t)stored;
        goto done;
    }
    EnterCriticalSection(&runtime->lock);
    value = runtime->next_token++;
    LeaveCriticalSection(&runtime->lock);
    if (!TlsSetValue(runtime->token_key, (void *)(size_t)value) ||
        !TlsSetValue(runtime->initialized_key, (void *)1)) status = native_error();
done:
    SetLastError(saved_error);
    if (!status) *token = value;
    return status;
}
int mm_native_runtime_exchange_ticks(MMNativeRuntime *runtime, MMNativeTickSource source, MMNativeTickSource *previous)
{
    MMNativeTickSource old;
    EnterCriticalSection(&runtime->lock);
    old = runtime->source; runtime->source = source;
    LeaveCriticalSection(&runtime->lock);
    if (previous) *previous = old;
    return 0;
}
int mm_native_runtime_ticks(MMNativeRuntime *runtime, MMU32 *ticks, void *context, MMNativeTickInvoke invoke)
{
    MMNativeTickSource source;
    MMRuntimeCounter now, milliseconds;
    MMRuntimeCounter zero;
    MMU32 value;
    int status, negative;
    EnterCriticalSection(&runtime->lock); source = runtime->source; LeaveCriticalSection(&runtime->lock);
    if (source) {
        if (invoke) { status = invoke(context, source, &value); if (status) return status; }
        else value = source();
    } else {
        status = counter(&now);
        if (status) return status;
        negative = (now.high & 0x80000000UL) != 0;
        if (negative) { zero.low = zero.high = 0; now = mm_runtime_counter_subtract(zero, now); }
        if (!mm_runtime_counter_scale(now, 1000, runtime->frequency, &milliseconds)) return ERROR_ARITHMETIC_OVERFLOW;
        value = negative ? (MMU32)0 - milliseconds.low : milliseconds.low;
    }
    *ticks = value;
    return 0;
}
void mm_native_break_into_debugger(void) { DebugBreak(); }
