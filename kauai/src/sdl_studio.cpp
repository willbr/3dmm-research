/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include "sdl_studio.h"
#include "frame.h"
#undef size
ASSERTNAME
namespace KauaiSDLBridge
{
void Present()
{
    auto &host = KauaiSDL::Main();
    if (!host.Active())
        return;
    Paint();
    auto root = GraphicsObject::PgobFromHwnd(static_cast<CoreNative::WindowHandle>(host.WindowToken()));
    if (vpappb && root && root->Pgpt())
    {
        GraphicsEnvironment environment(root->Pgpt());
        vpappb->DrawHostOverlay(&environment);
    }
    GraphicsPort::Flush();
    host.Present();
}
void Paint()
{
    static bool painting = false;
    auto &host = KauaiSDL::Main();
    auto window = static_cast<CoreNative::WindowHandle>(host.WindowToken());
    auto root = window ? GraphicsObject::PgobFromHwnd(window) : nullptr;
    if (painting || !vpappb || !root || !root->Pgpt())
        return;
    std::vector<KauaiPixels::DamageRect> damage;
    if (!host.TakeDamage(damage) || damage.empty())
        return;
    painting = true;
    struct PaintGuard
    {
        bool &active;
        ~PaintGuard()
        {
            active = false;
        }
    } guard{painting};
    // Kauai's update entry point accepts a rectangle. Retain exact damage for
    // validation/scrolling, then coalesce this paint pass to its bounds.
    RC bounds(damage[0].left, damage[0].top, damage[0].right, damage[0].bottom);
    for (const auto &part : damage)
    {
        RC rectangle(part.left, part.top, part.right, part.bottom);
        bounds.Union(&rectangle);
    }
    vpappb->UpdateHwnd(window, &bounds);
}
namespace
{
KauaiPixels::DamageRect LogicalRect(const void *rectangle)
{
    const auto &r = *static_cast<const SystemRectangle *>(rectangle);
    return {r.left, r.top, r.right, r.bottom};
}
} // namespace
bool Invalidate(void *window, const void *rectangle)
{
    auto &host = KauaiSDL::Main();
    if (!host.IsWindow(window))
        return false;
    if (rectangle)
    {
        const auto r = LogicalRect(rectangle);
        host.Invalidate(&r);
    }
    else
        host.Invalidate();
    return true;
}
bool Validate(void *window, const void *rectangle)
{
    auto &host = KauaiSDL::Main();
    if (!host.IsWindow(window))
        return false;
    if (rectangle)
    {
        const auto r = LogicalRect(rectangle);
        host.Validate(&r);
    }
    else
        host.Validate();
    return true;
}
bool DamageBounds(void *window, void *rectangle)
{
    auto &host = KauaiSDL::Main();
    if (!host.IsWindow(window))
        return false;
    KauaiPixels::DamageRect r;
    host.DamageBounds(r);
    *static_cast<SystemRectangle *>(rectangle) = {r.left, r.top, r.right, r.bottom};
    return true;
}
bool Update(void *window)
{
    if (!KauaiSDL::Main().IsWindow(window))
        return false;
    Paint();
    return true;
}
bool ScrollDamage(void *window, const void *rectangle, int dx, int dy)
{
    auto &host = KauaiSDL::Main();
    if (!host.IsWindow(window))
        return false;
    host.ScrollDamage(LogicalRect(rectangle), dx, dy);
    return true;
}
} // namespace KauaiSDLBridge
