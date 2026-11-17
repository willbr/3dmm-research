/* Core-platform C++ value/exception and legacy-layout adapters. */
#include "core_platform.h"
#include "mm/heap.h"
#include <exception>
#include <system_error>
namespace {
const MMHeapLayout heapLayout{sizeof(std::max_align_t)};
void Check(int error)
{ if (error) throw std::system_error(error, std::system_category()); }
MMNativeRuntime *ProcessRuntime()
{
    // Like the legacy allocator locks, this binding must survive global
    // destructors. Explicit C consumers instead own/destroy their context.
    static MMNativeRuntime *runtime = [] {
        int error = 0;
        auto *created = mm_native_runtime_create(&error);
        // Legacy clock/diagnostic callbacks have no native-error channel and
        // are also called from C. Do not unwind through those C callbacks.
        if (error) std::terminate();
        return created;
    }();
    return runtime;
}
struct TickInvocation { std::exception_ptr error; };
int InvokeTick(void *opaque, MMNativeTickSource source, MMU32 *value)
{
    auto &invocation = *static_cast<TickInvocation *>(opaque);
    try { *value = source(); return 0; }
    catch (...) { invocation.error = std::current_exception(); return 1; }
}
}
Mutex::Mutex()
{
    int error = 0;
    _mutex = mm_native_mutex_create(&error);
    Check(error);
}
Mutex::~Mutex() { mm_native_mutex_destroy(_mutex); }
void Mutex::Enter() { Check(mm_native_mutex_enter(_mutex)); }
void Mutex::Leave() { Check(mm_native_mutex_leave(_mutex)); }
KauaiPlatform::AutoResetEvent::AutoResetEvent()
{
    int error = 0;
    event = mm_native_event_create(&error);
    Check(error);
}
KauaiPlatform::AutoResetEvent::~AutoResetEvent() { mm_native_event_destroy(event); }
void KauaiPlatform::AutoResetEvent::Signal() { Check(mm_native_event_signal(event)); }
bool KauaiPlatform::AutoResetEvent::Wait(std::uint32_t milliseconds)
{
    int signaled = 0;
    Check(mm_native_event_wait(event, milliseconds, &signaled));
    return signaled != 0;
}
void *KauaiPlatform::Allocate(std::size_t bytes, bool clear)
{ return mm_heap_allocate(&heapLayout, nullptr, bytes, clear); }
void *KauaiPlatform::Reallocate(void *block, std::size_t bytes)
{ return mm_heap_reallocate(&heapLayout, nullptr, block, bytes); }
void KauaiPlatform::Free(void *block) { mm_heap_release(&heapLayout, nullptr, block); }
std::size_t KauaiPlatform::AllocationSize(const void *block) { return mm_heap_size(&heapLayout, block); }
long KauaiPlatform::ThreadToken()
{
    MMU32 token = 0;
    if (mm_native_runtime_thread_token(ProcessRuntime(), &token)) std::terminate();
    return static_cast<long>(token);
}
void KauaiPlatform::BreakIntoDebugger() { mm_native_break_into_debugger(); }
std::uint32_t KauaiPlatform::SystemTicks()
{
    MMU32 value = 0;
    TickInvocation invocation{};
    const auto error = mm_native_runtime_ticks(ProcessRuntime(), &value, &invocation, InvokeTick);
    if (invocation.error) std::rethrow_exception(invocation.error);
    if (error) std::terminate(); // std::chrono::steady_clock::now() was noexcept.
    return value;
}
KauaiPlatform::TickSource KauaiPlatform::SetTickSource(TickSource source)
{
    TickSource previous = nullptr;
    if (mm_native_runtime_exchange_ticks(ProcessRuntime(), source, &previous)) std::terminate();
    return previous;
}
