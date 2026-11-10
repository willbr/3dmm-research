#include "studio_timing.h"
#include "core_platform.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
namespace
{
std::uint32_t tickOffset;
std::uint32_t LegacyTicks()
{
    return GetTickCount() + tickOffset;
}
} // namespace
void StudioTiming::UseLegacyWindowsTicks()
{
    static bool installed = false;
    if (installed)
        return;
    // The master application clock is constructed before main. Preserve that
    // epoch while restoring the original Windows timer's resolution and phase.
    tickOffset = KauaiPlatform::SystemTicks() - GetTickCount();
    KauaiPlatform::SetTickSource(LegacyTicks);
    installed = true;
}
