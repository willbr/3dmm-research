/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_HOST_H
#define KAUAI_SDL_HOST_H
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "damage_map.h"
#include "color.h"

namespace KauaiSDL
{
std::uint64_t SystemMemoryBytes();
enum class EventKind
{
    none,
    quit,
    exposed,
    resized,
    focus,
    motion,
    buttonDown,
    buttonUp,
    keyDown,
    keyUp,
    text,
    clipboard
};
enum Modifier : std::uint32_t
{
    shift = 1,
    control = 2,
    alt = 4
};
struct Event
{
    EventKind kind = EventKind::none;
    float x = 0, y = 0;
    std::uint32_t button = 0, key = 0, modifiers = 0;
    std::uint64_t timestamp = 0;
    bool active = false, repeat = false;
    std::string text;
};
struct CursorStatus
{
    std::uint64_t created = 0, selected = 0, current = 0;
    std::size_t live = 0;
    bool visible = false;
};
struct PointerStatus
{
    bool captureRequested = false, captureActive = false;
    bool confinementRequested = false, confinementActive = false;
    bool suspended = false;
};
struct PresentationStatus
{
    float x = 0, y = 0, width = 0, height = 0;
    int sourceX = 0, sourceY = 0, sourceWidth = 0, sourceHeight = 0;
    int outputWidth = 0, outputHeight = 0;
    int frameWidth = 0, frameHeight = 0;
};
struct WindowStatus
{
    bool minimized = false, maximized = false, fullscreen = false;
    bool shown = false, keyboardFocus = false;
};
// SDL owns presentation and the event queue. Pixels are a borrowed, top-down
// XRGB8888 framebuffer; no Kauai, GDI or platform headers cross this boundary.
class Host
{
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::uint64_t nextCursor = 1;

  public:
    Host();
    ~Host();
    Host(const Host &) = delete;
    Host &operator=(const Host &) = delete;
    bool Create(const char *title, int width, int height, bool hidden = false);
    // Transitional token for the Windows rasterizer; never owned by callers.
    void *NativeWindow() const;
    // Opaque window identity used by Kauai: HWND on Windows, SDL window token
    // elsewhere (including headless drivers). Never dereference this token.
    void *WindowToken() const;
    std::uint32_t WindowID() const;
    void ResumeAfterModal();
    bool SetFullscreen(bool fullscreen);
    // Fit the available area by default; opt into whole pixel multiples in
    // windowed, maximized and fullscreen modes.
    bool SetIntegerScaling(bool enabled);
    bool IntegerScaling() const;
    // Present a canvas subrectangle (nullptr restores the complete canvas).
    // Input and desktop mappings continue to use complete-canvas coordinates.
    bool SetPresentationSource(const KauaiPixels::DamageRect *rectangle);
    // Borrow a higher-resolution image of the selected logical source area.
    // nullptr restores the canvas. Caller retains storage until reset.
    // nearest keeps bitmap UI art crisp when the image includes Studio controls.
    // A nonzero serial identifies immutable frame contents until the serial changes.
    bool SetPresentationPixels(const void *pixels, int width = 0, int height = 0, int pitch = 0, bool nearest = false,
                               std::uint64_t serial = 0);
    bool SetIndexedPresentationPixels(const void *pixels, int width, int height, int pitch, const Color *palette,
                                      bool nearest = false, std::uint64_t serial = 0);
    bool CopyPresentationPixels(std::vector<std::uint32_t> &pixels, int &width, int &height) const;
    bool SetMaximized(bool maximized);
    // Actual window-manager state; requests may take effect asynchronously.
    // Leaves status unchanged when no window exists.
    bool WindowState(WindowStatus &status) const;
    // Show/raise the window, restoring it only when minimized. A background
    // maximized window must retain its size when opening a Studio dialog.
    bool EnsureInteractive();
    bool SetWindowSize(int width, int height);
    // Largest requested whole-canvas multiple inside the usable desktop,
    // including window decorations. Fullscreen keeps its selected mode.
    static int WindowScaleToFit(int requested, int width, int height, int availableWidth, int availableHeight);
    bool FitWindowScale(int requested);
    // Client size in window units, independent of canvas size/display density.
    bool WindowSize(int &width, int &height) const;
    bool SetTitle(const std::string &utf8);
    bool Title(std::string &utf8) const;
    bool Raise();
    // Desktop coordinates use SDL window units, including display density.
    // Mapping includes the selected scaling mode, letterboxing
    // and the render viewport.
    // Output is unchanged on failure (including unavailable desktop positions).
    bool CanvasToDesktop(float x, float y, float &desktopX, float &desktopY) const;
    bool DesktopToCanvas(float x, float y, float &canvasX, float &canvasY) const;
    void Close();
    bool IsWindow(void *nativeWindow) const;
    bool Active() const;
    int Width() const;
    int Height() const;
    bool Presentation(PresentationStatus &status) const;
    void BindPixels(const void *pixels, int pitch);
    void UnbindPixels(const void *pixels);
    bool Present(bool force = false);
    bool Poll(Event &event, int timeoutMs = 0);
    void Mouse(float &x, float &y, bool &down) const;
    std::uint32_t Modifiers() const;
    bool PointerInside() const;
    void SetDoubleClickTime(std::uint32_t milliseconds);
    std::uint32_t DoubleClickTime() const;
    // Native modal loops temporarily own application activation. Drop their
    // queued window-focus changes when returning to SDL dispatch.
    void SuspendFocusEvents(bool suspended);
    // Cap repeated keys locally to a one-second delay and 400 ms interval.
    void SetSlowKeyRepeat(bool slow);
    bool SlowKeyRepeat() const;
    void FlushInput(bool mouse, bool keys);
    bool SaveBMP(const char *path, bool includeCursor = false);
    bool CopyPixels(std::vector<std::uint32_t> &pixels, int &width, int &height) const;
    // Automation goes through SDL's event queue, just like device input.
    bool PushPointer(float x, float y, bool down, std::uint32_t button = 1);
    bool PushKey(std::uint32_t key, bool down, std::uint32_t modifiers = 0);
    bool PushText(const std::string &utf8);
    // UTF-8 text, owned by the caller after retrieval. Clipboard events mark
    // external changes; ownership preserves Studio's richer internal data.
    bool GetClipboardText(std::string &utf8) const;
    bool SetClipboardText(const std::string &utf8);
    bool OwnsClipboard() const;
    // IDs are owned by this host and remain invalid after Close/restart.
    // Borrowed MSB-first planes use (background AND andBits) XOR xorBits.
    // Custom cursors are composited during presentation, preserving inversion
    // without native drawing and without modifying the borrowed framebuffer.
    std::uint64_t CreateCursor(const std::uint8_t *andBits, const std::uint8_t *xorBits, int width, int height,
                               int pitch, int hotX, int hotY);
    void DestroyCursor(std::uint64_t cursor);
    bool SetCursor(std::uint64_t cursor);
    bool SetSystemCursor(bool waiting);
    bool SetCursorVisible(bool visible);
    bool WarpPointerGlobal(float x, float y);
    // Logical canvas coordinates; synchronizes tracking before motion arrives.
    bool WarpPointer(float x, float y);
    // Requests survive temporary focus loss; suspension releases both until
    // tracking resumes. Actual capture/grab depends on backend and focus.
    bool CapturePointer(bool capture);
    bool ConfinePointer(bool confined);
    void SuspendPointer(bool suspended);
    PointerStatus Pointer() const;
    CursorStatus Cursors() const;
    // Logical canvas damage, independent of native window coordinates.
    void Invalidate(const KauaiPixels::DamageRect *rectangle = nullptr);
    void Validate(const KauaiPixels::DamageRect *rectangle = nullptr);
    bool DamageBounds(KauaiPixels::DamageRect &rectangle) const;
    bool TakeDamage(std::vector<KauaiPixels::DamageRect> &rectangles);
    bool ScrollDamage(const KauaiPixels::DamageRect &rectangle, std::int32_t dx, std::int32_t dy);
    const char *Error() const;
};
Host &Main();
} // namespace KauaiSDL
#endif
