#include "core_platform.h"
#include "studio_timing.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
int main()
{
    const auto before = KauaiPlatform::SystemTicks();
    StudioTiming::UseLegacyWindowsTicks();
    if (KauaiPlatform::SystemTicks() - before > 32)
        return 1;
    std::uint32_t offset = 0;
    unsigned checked = 0;
    for (unsigned i = 0; i < 100; ++i)
    {
        const auto a = GetTickCount();
        const auto tick = KauaiPlatform::SystemTicks();
        const auto b = GetTickCount();
        if (a == b)
        {
            if (checked && tick - a != offset)
                return 1;
            offset = tick - a;
            ++checked;
        }
        Sleep(1);
    }
    if (checked < 10)
        return 1;
    std::puts("Studio clock preserves its epoch and matches native Windows tick edges.");
    return 0;
}
