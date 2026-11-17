#include "core_platform.h"
#include <cassert>
#include <chrono>
#include <type_traits>
struct LegacyEvent { std::mutex mutex; std::condition_variable changed; bool signaled; };
static_assert(sizeof(Mutex) == sizeof(std::recursive_mutex), "legacy mutex allocation profile");
static_assert(sizeof(KauaiPlatform::AutoResetEvent) == sizeof(LegacyEvent), "legacy event allocation profile");
static_assert(!std::is_copy_constructible<Mutex>::value && !std::is_move_constructible<Mutex>::value, "mutex ownership");
static_assert(!std::is_copy_constructible<KauaiPlatform::AutoResetEvent>::value, "event ownership");
namespace {
struct TickFailure {};
std::uint32_t Throws() { throw TickFailure{}; }
std::uint32_t Resets()
{
    assert(KauaiPlatform::SetTickSource(nullptr) == Resets);
    return 42;
}
std::uint32_t NativeTicks()
{
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}
int main()
{
    for (unsigned i = 0; i < 1000; ++i) {
        const auto before = NativeTicks(), actual = KauaiPlatform::SystemTicks(), after = NativeTicks();
        assert(static_cast<std::uint32_t>(actual - before) <= static_cast<std::uint32_t>(after - before));
    }
    const auto previous = KauaiPlatform::SetTickSource(Throws);
    bool caught = false;
    try { KauaiPlatform::SystemTicks(); }
    catch (const TickFailure &) { caught = true; }
    assert(caught && KauaiPlatform::SetTickSource(Resets) == Throws);
    assert(KauaiPlatform::SystemTicks() == 42);
    assert(KauaiPlatform::SetTickSource(previous) == nullptr);
    return 0;
}
