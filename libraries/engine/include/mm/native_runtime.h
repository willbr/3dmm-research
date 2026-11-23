#ifndef MM_NATIVE_RUNTIME_H
#define MM_NATIVE_RUNTIME_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMNativeMutex MMNativeMutex;
typedef struct MMNativeEvent MMNativeEvent;
typedef struct MMNativeThread MMNativeThread;
typedef struct MMNativeRuntime MMNativeRuntime;
/* Factories return NULL and optionally publish a native error. Other integer
 * results are zero on success, native error otherwise. Owners are independent
 * of Kauai's diagnostic heap/locks. Destroy only with callers/waiters stopped;
 * a runtime also requires its other registered threads to have exited. The
 * destroying thread may still own a token. No copying. */
MMNativeMutex *mm_native_mutex_create(int *error);
void mm_native_mutex_destroy(MMNativeMutex *);
int mm_native_mutex_enter(MMNativeMutex *);
int mm_native_mutex_leave(MMNativeMutex *);
MMNativeEvent *mm_native_event_create(int *error);
void mm_native_event_destroy(MMNativeEvent *);
int mm_native_event_signal(MMNativeEvent *);
/* Coalescing latch; a signal releases one waiter or remains pending. Wait
 * consumes only a true latch. UINT32_MAX waits indefinitely. Timed waits retain
 * their deadline across spurious wakeups. Error leaves signaled untouched. */
int mm_native_event_wait(MMNativeEvent *, MMU32 milliseconds, int *signaled);
/* The entry borrows its context until join and must return normally. The
 * owner joins once from another thread. Successful join consumes the owner. */
typedef void (*MMNativeThreadEntry)(void *context);
MMNativeThread *mm_native_thread_start(MMNativeThreadEntry, void *context, int *error);
int mm_native_thread_join(MMNativeThread *);
typedef MMU32 (*MMNativeTickSource)(void);
typedef int (*MMNativeTickInvoke)(void *context, MMNativeTickSource, MMU32 *ticks);
MMNativeRuntime *mm_native_runtime_create(int *error);
void mm_native_runtime_destroy(MMNativeRuntime *);
/* Context-local stable per-thread diagnostic identity, starting at one and
 * wrapping at 32 bits. Zero after wrap is a valid initialized token. Queries
 * preserve the calling thread's Windows last-error/POSIX errno value. */
int mm_native_runtime_thread_token(MMNativeRuntime *, MMU32 *token);
/* Millisecond steady-clock epoch, wrapping at 32 bits. Callback is snapshotted
 * under the context lock, then invoked unlocked. NULL restores the native
 * clock. Configure while users are stopped; existing clock bases are unchanged.
 * Optional invoke adapts the source call (e.g. containing C++ exceptions); it
 * returns zero on success. No C callback may throw. Errors preserve outputs. */
int mm_native_runtime_ticks(MMNativeRuntime *, MMU32 *ticks, void *context, MMNativeTickInvoke invoke);
int mm_native_runtime_exchange_ticks(MMNativeRuntime *, MMNativeTickSource, MMNativeTickSource *previous);
void mm_native_break_into_debugger(void);
#ifdef __cplusplus
}
#endif
#endif
