/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include "sdl_surface.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <list>
#include <map>
#include <limits>

namespace KauaiSDL
{
std::uint64_t SystemMemoryBytes()
{
    const auto megabytes = SDL_GetSystemRAM();
    return megabytes > 0 ? static_cast<std::uint64_t>(megabytes) * 1024 * 1024 : 0;
}
namespace
{
std::uint32_t ModifiersFromSDL(SDL_Keymod value)
{
    return ((value & SDL_KMOD_SHIFT) ? shift : 0) | ((value & SDL_KMOD_CTRL) ? control : 0) |
           ((value & SDL_KMOD_ALT) ? alt : 0);
}
std::uint32_t VirtualKey(SDL_Scancode key)
{
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z)
        return 'A' + key - SDL_SCANCODE_A;
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9)
        return '1' + key - SDL_SCANCODE_1;
    if (key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F12)
        return 0x70 + key - SDL_SCANCODE_F1;
    if (key >= SDL_SCANCODE_KP_1 && key <= SDL_SCANCODE_KP_9)
        return 0x61 + key - SDL_SCANCODE_KP_1;
    switch (key)
    {
    case SDL_SCANCODE_0:
        return '0';
    case SDL_SCANCODE_BACKSPACE:
        return 8;
    case SDL_SCANCODE_TAB:
        return 9;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return 13;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
        return 16;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
        return 17;
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
        return 18;
    case SDL_SCANCODE_ESCAPE:
        return 27;
    case SDL_SCANCODE_SPACE:
        return 32;
    case SDL_SCANCODE_PAGEUP:
        return 33;
    case SDL_SCANCODE_PAGEDOWN:
        return 34;
    case SDL_SCANCODE_END:
        return 35;
    case SDL_SCANCODE_HOME:
        return 36;
    case SDL_SCANCODE_LEFT:
        return 37;
    case SDL_SCANCODE_UP:
        return 38;
    case SDL_SCANCODE_RIGHT:
        return 39;
    case SDL_SCANCODE_DOWN:
        return 40;
    case SDL_SCANCODE_INSERT:
        return 45;
    case SDL_SCANCODE_DELETE:
        return 46;
    case SDL_SCANCODE_KP_0:
        return 0x60;
    case SDL_SCANCODE_KP_MULTIPLY:
        return 0x6a;
    case SDL_SCANCODE_KP_PLUS:
        return 0x6b;
    case SDL_SCANCODE_KP_MINUS:
        return 0x6d;
    case SDL_SCANCODE_KP_PERIOD:
        return 0x6e;
    case SDL_SCANCODE_KP_DIVIDE:
        return 0x6f;
    case SDL_SCANCODE_PAUSE:
        return 19;
    case SDL_SCANCODE_CAPSLOCK:
        return 20;
    case SDL_SCANCODE_PRINTSCREEN:
        return 44;
    case SDL_SCANCODE_NUMLOCKCLEAR:
        return 144;
    case SDL_SCANCODE_SCROLLLOCK:
        return 145;
    case SDL_SCANCODE_SEMICOLON:
        return 186;
    case SDL_SCANCODE_EQUALS:
        return 187;
    case SDL_SCANCODE_COMMA:
        return 188;
    case SDL_SCANCODE_MINUS:
        return 189;
    case SDL_SCANCODE_PERIOD:
        return 190;
    case SDL_SCANCODE_SLASH:
        return 191;
    case SDL_SCANCODE_GRAVE:
        return 192;
    case SDL_SCANCODE_LEFTBRACKET:
        return 219;
    case SDL_SCANCODE_BACKSLASH:
        return 220;
    case SDL_SCANCODE_RIGHTBRACKET:
        return 221;
    case SDL_SCANCODE_APOSTROPHE:
        return 222;
    default:
        return 0;
    }
}
} // namespace
struct Host::Impl
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    SDL_Texture *presentationTexture = nullptr;
    const void *presentationPixels = nullptr;
    int presentationWidth = 0, presentationHeight = 0, presentationPitch = 0;
    bool presentationIndexed = false, presentationNearest = false, presentationDirty = true;
    bool uploadedIndexed = false;
    int uploadedWidth = 0, uploadedHeight = 0;
    std::uint64_t presentationSerial = 0;
    std::array<Color, 256> presentationColors{};
    SDL_Palette *presentationPalette = nullptr;
    std::vector<std::uint32_t> presentationRgb;
    std::uint64_t uploadedCursor = 0;
    int uploadedMouseX = 0, uploadedMouseY = 0;
    void *native = nullptr;
    const void *pixels = nullptr;
    int width = 0, height = 0, pitch = 0;
    SDL_FRect source = {};
    float mouseX = 0, mouseY = 0;
    bool mouseDown = false, initialized = false, clipboardOwned = false, focusSuspended = false;
    bool integerScaling = false;
    bool captureRequested = false, confinementRequested = false, pointerSuspended = false;
    bool captureApplied = false, focused = false;
    std::uint32_t modifiers = 0;
    std::uint32_t doubleClickTime = 500;
    struct HeldKey
    {
        std::uint64_t first = 0, last = 0;
        bool down = false;
    };
    std::array<HeldKey, SDL_SCANCODE_COUNT> heldKeys{};
    bool slowKeyRepeat = false, suppressRepeatedText = false;
    std::uint64_t lastPresent = 0;
    std::list<std::string> queuedText;
    struct CursorImage
    {
        int width, height, hotX, hotY;
        std::vector<std::uint8_t> andBits, xorBits;
    };
    std::map<std::uint64_t, CursorImage> cursors;
    SDL_Cursor *waitCursor = nullptr, *transparentCursor = nullptr;
    bool cursorVisible = true, mouseInside = false, triedTransparentCursor = false;
    std::vector<std::uint32_t> cursorPixels;
    KauaiPixels::DamageMap damage;
    std::uint64_t createdCursors = 0, selectedCursors = 0, currentCursor = 0;
    ~Impl()
    {
        if (captureApplied)
            SDL_CaptureMouse(false);
        // SDL copies the event structure, not an application's text buffer.
        // Remove our queued pointers before releasing their owned storage.
        if (!queuedText.empty())
            SDL_FilterEvents(
                [](void *data, SDL_Event *event) {
                    auto self = static_cast<Impl *>(data);
                    if (event->type == SDL_EVENT_TEXT_INPUT)
                        for (const auto &text : self->queuedText)
                            if (event->text.text == text.c_str())
                                return false;
                    return true;
                },
                this);
        if (initialized)
            SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_DestroyCursor(waitCursor);
        SDL_DestroyCursor(transparentCursor);
        SDL_DestroyTexture(texture);
        SDL_DestroyTexture(presentationTexture);
        SDL_DestroyPalette(presentationPalette);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    bool Finish(int w, int h)
    {
        width = w;
        height = h;
        source = {0, 0, float(w), float(h)};
        if (!damage.Init(w, h))
            return false;
        if (!window || !SDL_SetWindowMinimumSize(window, w, h))
            return false;
        damage.Invalidate();
        renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
        if (!renderer)
            return false;
        if (!SetPresentation())
            return false;
        SDL_SetRenderVSync(renderer, 0);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!texture)
            return false;
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_StartTextInput(window);
        const auto driver = SDL_GetCurrentVideoDriver();
        focused =
            (driver && std::strcmp(driver, "dummy") == 0) || (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        return true;
    }
    bool SetPresentation()
    {
        // Apply the same scaling preference in every window mode.
        const auto mode = integerScaling ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX;
        return renderer && SDL_SetRenderLogicalPresentation(renderer, int(source.w), int(source.h), mode);
    }
    bool ApplyPointer()
    {
        if (!window)
            return false;
        const bool enabled = focused && !pointerSuspended && !focusSuspended;
        const bool capture = enabled && captureRequested && SDL_GetKeyboardFocus() == window;
        bool success = true;
        if (capture != captureApplied)
        {
            success = SDL_CaptureMouse(capture);
            if (success)
                captureApplied = capture;
        }
        return SDL_SetWindowMouseGrab(window, enabled && confinementRequested) && success;
    }
    bool AcceptKey(const SDL_KeyboardEvent &key)
    {
        if (key.scancode < 0 || key.scancode >= SDL_SCANCODE_COUNT)
            return true;
        auto &held = heldKeys[key.scancode];
        suppressRepeatedText = false;
        if (!key.down)
        {
            held = {};
            return true;
        }
        const auto now = key.timestamp / 1000000;
        if (!key.repeat || !held.down || now < held.first || now < held.last)
        {
            held = {now, now, true};
            return true;
        }
        if (slowKeyRepeat && (now - held.first < 1000 || now - held.last < 400))
        {
            suppressRepeatedText = true;
            return false;
        }
        held.last = now;
        return true;
    }
    void ResetKeys()
    {
        heldKeys = {};
        modifiers = 0;
        suppressRepeatedText = false;
    }
    void ApplyCursor()
    {
        if (!currentCursor)
            return;
        if (mouseInside && !transparentCursor)
            SDL_HideCursor();
        else
        {
            SDL_SetCursor(mouseInside ? transparentCursor : SDL_GetDefaultCursor());
            cursorVisible ? SDL_ShowCursor() : SDL_HideCursor();
        }
    }
    void MouseInside(bool inside)
    {
        if (mouseInside != inside)
        {
            mouseInside = inside;
            ApplyCursor();
        }
    }
    bool StreamPixels(SDL_Texture *image, const void *data, int imageWidth, int imageHeight, int imagePitch, int bytes)
    {
        void *locked = nullptr;
        int lockedPitch = 0;
        if (!SDL_LockTexture(image, nullptr, &locked, &lockedPitch))
            return false;
        for (int y = 0; y < imageHeight; ++y)
            std::memcpy(static_cast<std::uint8_t *>(locked) + std::size_t(y) * lockedPitch,
                        static_cast<const std::uint8_t *>(data) + std::size_t(y) * imagePitch,
                        std::size_t(imageWidth) * bytes);
        SDL_UnlockTexture(image);
        return true;
    }
    bool UploadImage(SDL_Texture *image, const void *data, int imageWidth, int imageHeight, int imagePitch,
                     const SDL_FRect &logical, bool includeCursor)
    {
        if (!data || imageWidth < 1 || imageHeight < 1 || imageWidth > INT32_MAX / 4 || imagePitch < imageWidth * 4 ||
            logical.w <= 0 || logical.h <= 0 ||
            std::uint64_t(imagePitch) * imageHeight > std::numeric_limits<std::size_t>::max())
            return false;
        const auto found = cursors.find(currentCursor);
        if (!includeCursor || !cursorVisible || !mouseInside || found == cursors.end())
            return StreamPixels(image, data, imageWidth, imageHeight, imagePitch, 4);
        try
        {
            cursorPixels.resize(std::size_t(imageWidth) * imageHeight);
            for (int y = 0; y < imageHeight; ++y)
                std::memcpy(cursorPixels.data() + std::size_t(y) * imageWidth,
                            static_cast<const std::uint8_t *>(data) + std::size_t(y) * imagePitch, imageWidth * 4);
            const auto &cursor = found->second;
            const int left = static_cast<int>(mouseX) - cursor.hotX, top = static_cast<int>(mouseY) - cursor.hotY;
            const int firstX = std::max(0, int(std::floor((left - logical.x) * imageWidth / logical.w)));
            const int firstY = std::max(0, int(std::floor((top - logical.y) * imageHeight / logical.h)));
            const int lastX =
                std::min(imageWidth, int(std::ceil((left + cursor.width - logical.x) * imageWidth / logical.w)));
            const int lastY =
                std::min(imageHeight, int(std::ceil((top + cursor.height - logical.y) * imageHeight / logical.h)));
            for (int y = firstY; y < lastY; ++y)
                for (int x = firstX; x < lastX; ++x)
                {
                    const int cx = int((x + 0.5) * logical.w / imageWidth + logical.x) - left;
                    const int cy = int((y + 0.5) * logical.h / imageHeight + logical.y) - top;
                    if (cx < 0 || cy < 0 || cx >= cursor.width || cy >= cursor.height)
                        continue;
                    const auto offset = cy * (cursor.width / 8) + cx / 8;
                    const auto bit = 0x80 >> (cx & 7);
                    auto &pixel = cursorPixels[std::size_t(y) * imageWidth + x];
                    if (!(cursor.andBits[offset] & bit))
                        pixel &= 0xFF000000;
                    if (cursor.xorBits[offset] & bit)
                        pixel ^= 0xFFFFFF;
                }
            return StreamPixels(image, cursorPixels.data(), imageWidth, imageHeight, imageWidth * 4, 4);
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
    }
    bool Upload(bool includeCursor)
    {
        return UploadImage(texture, pixels, width, height, pitch, {0, 0, float(width), float(height)}, includeCursor);
    }
    bool UploadPresentation(bool includeCursor)
    {
        if (!presentationPixels)
            return true;
        const auto cursor =
            includeCursor && cursorVisible && mouseInside && cursors.count(currentCursor) ? currentCursor : 0;
        const int x = int(mouseX), y = int(mouseY);
        if (presentationSerial && !presentationDirty && uploadedCursor == cursor &&
            (!cursor || (uploadedMouseX == x && uploadedMouseY == y)))
            return true;
        // Indexed textures let SDL perform palette lookup during presentation.
        // An AND/XOR cursor needs true-color pixels so inversion retains its
        // legacy meaning, including colors absent from the movie palette.
        const bool indexed = presentationIndexed && !cursor;
        if (!presentationTexture || indexed != uploadedIndexed || uploadedWidth != presentationWidth ||
            uploadedHeight != presentationHeight)
        {
            auto next = SDL_CreateTexture(renderer, indexed ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_XRGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, presentationWidth, presentationHeight);
            if (!next)
                return false;
            if (indexed)
            {
                if (!presentationPalette)
                    presentationPalette = SDL_CreatePalette(256);
                if (!presentationPalette || !SDL_SetTexturePalette(next, presentationPalette))
                {
                    SDL_DestroyTexture(next);
                    return false;
                }
            }
            SDL_SetTextureBlendMode(next, SDL_BLENDMODE_NONE);
            SDL_DestroyTexture(presentationTexture);
            presentationTexture = next;
            uploadedIndexed = indexed;
            uploadedWidth = presentationWidth;
            uploadedHeight = presentationHeight;
        }
        SDL_SetTextureScaleMode(presentationTexture,
                                presentationNearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
        bool success;
        if (indexed)
        {
            SDL_Color colors[256];
            for (int i = 0; i < 256; ++i)
                colors[i] = {presentationColors[i].bRed, presentationColors[i].bGreen, presentationColors[i].bBlue,
                             255};
            success = SDL_SetPaletteColors(presentationPalette, colors, 0, 256) &&
                      StreamPixels(presentationTexture, presentationPixels, presentationWidth, presentationHeight,
                                   presentationPitch, 1);
        }
        else
        {
            const void *data = presentationPixels;
            int row = presentationPitch;
            if (presentationIndexed)
            {
                try
                {
                    presentationRgb.resize(std::size_t(presentationWidth) * presentationHeight);
                }
                catch (const std::bad_alloc &)
                {
                    return false;
                }
                if (!ExpandPresentation(presentationRgb.data()))
                    return false;
                data = presentationRgb.data();
                row = presentationWidth * 4;
            }
            success = UploadImage(presentationTexture, data, presentationWidth, presentationHeight, row, source,
                                  includeCursor);
        }
        if (success)
        {
            presentationDirty = false;
            uploadedCursor = cursor;
            uploadedMouseX = x;
            uploadedMouseY = y;
        }
        return success;
    }
    bool ExpandPresentation(void *destination) const
    {
        const KauaiPixels::DamageRect bounds{0, 0, presentationWidth, presentationHeight};
        const KauaiPixels::PackedSurface input{const_cast<void *>(presentationPixels),
                                               presentationWidth,
                                               presentationHeight,
                                               presentationPitch,
                                               8,
                                               presentationColors.data(),
                                               256};
        const KauaiPixels::PackedSurface output{destination, presentationWidth, presentationHeight,
                                                presentationWidth * 4, 32};
        return BlitPixels(output, input, bounds, bounds);
    }
    bool BorrowPresentation(const void *data, int w, int h, int row, const Color *colors, bool nearest,
                            std::uint64_t serial)
    {
        if (!data)
        {
            presentationPixels = nullptr;
            presentationDirty = true;
            return true;
        }
        if (!renderer || w < 1 || h < 1 || w > 4096 || h > 4096 || row < w * (colors ? 1 : 4))
            return false;
        presentationDirty = presentationDirty || !serial || serial != presentationSerial ||
                            data != presentationPixels || w != presentationWidth || h != presentationHeight ||
                            row != presentationPitch || bool(colors) != presentationIndexed ||
                            nearest != presentationNearest ||
                            (colors && std::memcmp(colors, presentationColors.data(), sizeof(Color) * 256));
        presentationPixels = data;
        presentationWidth = w;
        presentationHeight = h;
        presentationPitch = row;
        presentationIndexed = colors != nullptr;
        presentationNearest = nearest;
        presentationSerial = serial;
        if (colors)
            std::copy_n(colors, 256, presentationColors.begin());
        return true;
    }
};
Host::Host() : impl(new Impl)
{
}
Host::~Host() = default;
Host &Main()
{
    static Host host;
    return host;
}
void Host::Close()
{
    impl.reset(new Impl);
}
bool Host::Create(const char *title, int width, int height, bool hidden)
{
    Close();
    SDL_SetMainReady();
    // Kauai owns capture for the lifetime of a tracking command. SDL's
    // automatic button capture could otherwise retain it during suspension.
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_AUTO_CAPTURE, "0", SDL_HINT_OVERRIDE);
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return false;
    impl->initialized = true;
    impl->window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0));
#ifdef _WIN32
    if (impl->window)
        impl->native =
            SDL_GetPointerProperty(SDL_GetWindowProperties(impl->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
    return impl->Finish(width, height);
}
void *Host::NativeWindow() const
{
    return impl->native;
}
void *Host::WindowToken() const
{
    return impl->native ? impl->native : impl->window;
}
std::uint32_t Host::WindowID() const
{
    return impl->window ? SDL_GetWindowID(impl->window) : 0;
}
bool Host::SetTitle(const std::string &utf8)
{
    return impl->window && utf8.find('\0') == std::string::npos && SDL_SetWindowTitle(impl->window, utf8.c_str());
}
bool Host::Title(std::string &utf8) const
{
    if (!impl->window)
        return false;
    const auto title = SDL_GetWindowTitle(impl->window);
    if (!title)
        return false;
    try
    {
        std::string next(title);
        utf8.swap(next);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
bool Host::Raise()
{
    return impl->window && SDL_RaiseWindow(impl->window);
}
bool Host::CanvasToDesktop(float x, float y, float &desktopX, float &desktopY) const
{
    int left, top;
    float wx, wy;
    if (!impl->renderer || !std::isfinite(x) || !std::isfinite(y) ||
        !SDL_GetWindowPosition(impl->window, &left, &top) ||
        !SDL_RenderCoordinatesToWindow(impl->renderer, x - impl->source.x, y - impl->source.y, &wx, &wy))
        return false;
    wx += left;
    wy += top;
    if (!std::isfinite(wx) || !std::isfinite(wy))
        return false;
    desktopX = wx;
    desktopY = wy;
    return true;
}
bool Host::DesktopToCanvas(float x, float y, float &canvasX, float &canvasY) const
{
    int left, top;
    float cx, cy;
    if (!impl->renderer || !std::isfinite(x) || !std::isfinite(y) ||
        !SDL_GetWindowPosition(impl->window, &left, &top) ||
        !SDL_RenderCoordinatesFromWindow(impl->renderer, x - left, y - top, &cx, &cy) || !std::isfinite(cx) ||
        !std::isfinite(cy))
        return false;
    canvasX = cx + impl->source.x;
    canvasY = cy + impl->source.y;
    return true;
}
void Host::ResumeAfterModal()
{
    if (!impl->window)
        return;
    SuspendFocusEvents(false);
    FlushInput(true, true);
    impl->ResetKeys();
    // The headless driver has no OS keyboard focus. Returning from its modal
    // UI must nevertheless resume the logical app and its sound queues.
    const auto driver = SDL_GetCurrentVideoDriver();
    impl->focused = (driver && std::strcmp(driver, "dummy") == 0) ||
                    (SDL_GetWindowFlags(impl->window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    float x,y;
    SDL_GetMouseState(&x,&y);
    SDL_RenderCoordinatesFromWindow(impl->renderer,x,y,&impl->mouseX,&impl->mouseY);
    impl->mouseX += impl->source.x;
    impl->mouseY += impl->source.y;
    impl->MouseInside(SDL_GetMouseFocus()==impl->window && impl->mouseX>=0 && impl->mouseX<impl->width &&
                      impl->mouseY>=0 && impl->mouseY<impl->height);
    impl->ApplyPointer();
    impl->ApplyCursor();
    impl->damage.Invalidate();
    SDL_Event focus{};
    focus.type = impl->focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    focus.window.windowID = SDL_GetWindowID(impl->window);
    SDL_PushEvent(&focus);
}
bool Host::SetFullscreen(bool fullscreen)
{
    if (!impl->window || !SDL_SetWindowFullscreen(impl->window, fullscreen) || !impl->SetPresentation())
        return false;
    impl->damage.Invalidate();
    return true;
}
bool Host::SetIntegerScaling(bool enabled)
{
    if (!impl->window || !impl->renderer)
        return false;
    const bool previous = impl->integerScaling;
    impl->integerScaling = enabled;
    if (!impl->SetPresentation())
    {
        impl->integerScaling = previous;
        return false;
    }
    impl->damage.Invalidate();
    return true;
}
bool Host::IntegerScaling() const
{
    return impl->integerScaling;
}
bool Host::SetPresentationSource(const KauaiPixels::DamageRect *rectangle)
{
    if (!impl->renderer)
        return false;
    SDL_FRect next{0, 0, float(impl->width), float(impl->height)};
    if (rectangle)
    {
        if (rectangle->left < 0 || rectangle->top < 0 || rectangle->right > impl->width ||
            rectangle->bottom > impl->height || rectangle->left >= rectangle->right ||
            rectangle->top >= rectangle->bottom)
            return false;
        next = {float(rectangle->left), float(rectangle->top), float(rectangle->right - rectangle->left),
                float(rectangle->bottom - rectangle->top)};
    }
    const auto previous = impl->source;
    impl->source = next;
    if (!impl->SetPresentation())
    {
        impl->source = previous;
        return false;
    }
    impl->damage.Invalidate();
    impl->presentationDirty = true;
    if (!rectangle)
        impl->presentationPixels = nullptr;
    return true;
}
bool Host::SetPresentationPixels(const void *pixels, int width, int height, int pitch, bool nearest,
                                 std::uint64_t serial)
{
    return impl->BorrowPresentation(pixels, width, height, pitch, nullptr, nearest, serial);
}
bool Host::SetIndexedPresentationPixels(const void *pixels, int width, int height, int pitch, const Color *palette,
                                        bool nearest, std::uint64_t serial)
{
    return palette && impl->BorrowPresentation(pixels, width, height, pitch, palette, nearest, serial);
}
bool Host::SetMaximized(bool maximized)
{
    if (!impl->window)
        return false;
    const auto flags = SDL_GetWindowFlags(impl->window);
    if (maximized && (flags & SDL_WINDOW_MAXIMIZED) && !(flags & SDL_WINDOW_MINIMIZED))
        return true;
    return maximized ? SDL_MaximizeWindow(impl->window) : SDL_RestoreWindow(impl->window);
}
bool Host::WindowState(WindowStatus &status) const
{
    if (!impl->window)
        return false;
    const auto flags = SDL_GetWindowFlags(impl->window);
    const auto driver = SDL_GetCurrentVideoDriver();
    const bool logicalFocus = driver && std::strcmp(driver, "dummy") == 0;
    status = {bool(flags & SDL_WINDOW_MINIMIZED), bool(flags & SDL_WINDOW_MAXIMIZED),
              bool(flags & SDL_WINDOW_FULLSCREEN), !(flags & SDL_WINDOW_HIDDEN),
              logicalFocus ? impl->focused : bool(flags & SDL_WINDOW_INPUT_FOCUS)};
    return true;
}
bool Host::EnsureInteractive()
{
    WindowStatus state;
    if (!WindowState(state))
        return false;
    if (!state.shown && !SDL_ShowWindow(impl->window))
        return false;
    if (state.minimized && !SDL_RestoreWindow(impl->window))
        return false;
    return SDL_RaiseWindow(impl->window);
}
bool Host::SetWindowSize(int width, int height)
{
    return impl->window && SDL_SetWindowSize(impl->window, width, height);
}
bool Host::Active() const
{
    return impl->renderer != nullptr;
}
bool Host::IsWindow(void *value) const
{
    return value && value == WindowToken() && Active();
}
int Host::Width() const
{
    return impl->width;
}
bool Host::WindowSize(int &width, int &height) const
{
    return impl->window && SDL_GetWindowSize(impl->window, &width, &height);
}
int Host::Height() const
{
    return impl->height;
}
bool Host::Presentation(PresentationStatus &status) const
{
    status = {};
    SDL_FRect rectangle;
    if (!impl->renderer || !SDL_GetRenderLogicalPresentationRect(impl->renderer, &rectangle) ||
        !SDL_GetRenderOutputSize(impl->renderer, &status.outputWidth, &status.outputHeight))
        return false;
    status.x = rectangle.x;
    status.y = rectangle.y;
    status.width = rectangle.w;
    status.height = rectangle.h;
    status.sourceX = int(impl->source.x);
    status.sourceY = int(impl->source.y);
    status.sourceWidth = int(impl->source.w);
    status.sourceHeight = int(impl->source.h);
    status.frameWidth = impl->presentationPixels ? impl->presentationWidth : int(impl->source.w);
    status.frameHeight = impl->presentationPixels ? impl->presentationHeight : int(impl->source.h);
    return true;
}
const char *Host::Error() const
{
    return SDL_GetError();
}
void Host::BindPixels(const void *pixels, int pitch)
{
    impl->pixels = pixels;
    impl->pitch = pitch;
}
void Host::UnbindPixels(const void *pixels)
{
    if (impl->pixels == pixels)
        impl->pixels = nullptr;
}
bool Host::Present(bool force)
{
    if (!impl->pixels || !impl->renderer)
        return false;
    const auto now = SDL_GetTicks();
    if (!force && now - impl->lastPresent < 16)
        return true;
    if (!impl->presentationPixels && !impl->Upload(true))
        return false;
    if (!impl->UploadPresentation(true))
        return false;
    SDL_SetRenderDrawColor(impl->renderer, 0, 0, 0, 255);
    if (!SDL_RenderClear(impl->renderer) ||
        !SDL_RenderTexture(impl->renderer, impl->presentationPixels ? impl->presentationTexture : impl->texture,
                           impl->presentationPixels ? nullptr : &impl->source, nullptr))
        return false;
    SDL_RenderPresent(impl->renderer);
    impl->lastPresent = now;
    return true;
}
bool Host::SaveBMP(const char *path, bool includeCursor)
{
    if (!impl->pixels || !impl->renderer || (!impl->presentationPixels && !impl->Upload(includeCursor)))
        return false;
    if (!impl->UploadPresentation(includeCursor))
        return false;
    SDL_RenderClear(impl->renderer);
    SDL_RenderTexture(impl->renderer, impl->presentationPixels ? impl->presentationTexture : impl->texture,
                      impl->presentationPixels ? nullptr : &impl->source, nullptr);
    auto surface = SDL_RenderReadPixels(impl->renderer, nullptr);
    const bool saved = surface && SDL_SaveBMP(surface, path);
    SDL_DestroySurface(surface);
    SDL_RenderPresent(impl->renderer);
    return saved;
}
bool Host::CopyPresentationPixels(std::vector<std::uint32_t> &pixels, int &width, int &height) const
{
    const bool high = impl->presentationPixels != nullptr;
    const auto source = high ? impl->presentationPixels : impl->pixels;
    if (!source)
        return false;
    width = high ? impl->presentationWidth : int(impl->source.w);
    height = high ? impl->presentationHeight : int(impl->source.h);
    const int pitch = high ? impl->presentationPitch : impl->pitch;
    const int left = high ? 0 : int(impl->source.x), top = high ? 0 : int(impl->source.y);
    pixels.resize(std::size_t(width) * height);
    if (high && impl->presentationIndexed)
        return impl->ExpandPresentation(pixels.data());
    for (int y = 0; y < height; ++y)
        std::memcpy(pixels.data() + std::size_t(y) * width,
                    static_cast<const std::uint8_t *>(source) + std::size_t(y + top) * pitch + left * 4, width * 4);
    return true;
}
bool Host::CopyPixels(std::vector<std::uint32_t> &pixels, int &width, int &height) const
{
    pixels.clear();
    width = height = 0;
    if (!impl->pixels)
        return false;
    width = impl->width;
    height = impl->height;
    pixels.resize(std::size_t(width) * height);
    for (int y = 0; y < height; ++y)
        std::memcpy(pixels.data() + std::size_t(y) * width,
                    static_cast<const std::uint8_t *>(impl->pixels) + y * impl->pitch, width * 4);
    return true;
}
bool Host::Poll(Event &event, int timeoutMs)
{
    event = {};
    SDL_Event next;
    bool available = timeoutMs ? SDL_WaitEventTimeout(&next, timeoutMs) : SDL_PollEvent(&next);
    while (available)
    {
        SDL_ConvertEventToRenderCoordinates(impl->renderer, &next);
        switch (next.type)
        {
        case SDL_EVENT_WINDOW_MOUSE_ENTER: {
            float x, y;
            SDL_GetMouseState(&x, &y);
            SDL_RenderCoordinatesFromWindow(impl->renderer, x, y, &impl->mouseX, &impl->mouseY);
            impl->mouseX += impl->source.x;
            impl->mouseY += impl->source.y;
            impl->MouseInside(impl->mouseX >= 0 && impl->mouseX < impl->width && impl->mouseY >= 0 &&
                              impl->mouseY < impl->height);
            break;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            impl->MouseInside(false);
            break;
        case SDL_EVENT_CLIPBOARD_UPDATE:
            impl->clipboardOwned = next.clipboard.owner;
            event.kind = EventKind::clipboard;
            event.active = next.clipboard.owner;
            break;
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            event.kind = EventKind::quit;
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            impl->damage.Invalidate();
            event.kind = EventKind::exposed;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            impl->damage.Invalidate();
            event.kind = EventKind::resized;
            break;
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            if (next.window.windowID == SDL_GetWindowID(impl->window))
            {
                // Also follow transitions requested by the desktop/window manager.
                if (!impl->SetPresentation())
                    return false;
                impl->damage.Invalidate();
                event.kind = EventKind::resized;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            event.kind = EventKind::focus;
            event.active = next.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            impl->focused = event.active;
            impl->ApplyPointer();
            if (!event.active)
            {
                impl->mouseDown = false;
                impl->ResetKeys();
                impl->MouseInside(false);
            }
            else
                impl->modifiers = ModifiersFromSDL(SDL_GetModState());
            if (impl->focusSuspended)
                event.kind = EventKind::none;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            event.kind = EventKind::motion;
            event.x = next.motion.x + impl->source.x;
            event.y = next.motion.y + impl->source.y;
            impl->mouseX = event.x;
            impl->mouseY = event.y;
            impl->MouseInside(event.x >= 0 && event.x < impl->width && event.y >= 0 && event.y < impl->height);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            event.kind = next.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventKind::buttonDown : EventKind::buttonUp;
            event.x = next.button.x + impl->source.x;
            event.y = next.button.y + impl->source.y;
            event.button = next.button.button;
            impl->mouseX = event.x;
            impl->mouseY = event.y;
            impl->MouseInside(event.x >= 0 && event.x < impl->width && event.y >= 0 && event.y < impl->height);
            if (event.button == SDL_BUTTON_LEFT)
                impl->mouseDown = event.kind == EventKind::buttonDown;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            event.kind = next.type == SDL_EVENT_KEY_DOWN ? EventKind::keyDown : EventKind::keyUp;
            event.key = VirtualKey(next.key.scancode);
            event.repeat = next.key.repeat;
            impl->modifiers = ModifiersFromSDL(next.key.mod);
            if (!impl->AcceptKey(next.key))
                event.kind = EventKind::none;
            break;
        case SDL_EVENT_TEXT_INPUT:
            event.kind = impl->suppressRepeatedText ? EventKind::none : EventKind::text;
            event.text = next.text.text;
            for (auto owned = impl->queuedText.begin(); owned != impl->queuedText.end(); ++owned)
                if (next.text.text == owned->c_str())
                {
                    event.kind = EventKind::text; // Explicit queued text is not a keyboard repeat.
                    impl->queuedText.erase(owned);
                    break;
                }
            break;
        default:
            break;
        }
        if (event.kind != EventKind::none)
        {
            event.timestamp = next.common.timestamp / 1000000;
            event.modifiers = impl->modifiers;
            return true;
        }
        available = SDL_PollEvent(&next);
    }
    return false;
}
void Host::Mouse(float &x, float &y, bool &down) const
{
    x = impl->mouseX;
    y = impl->mouseY;
    down = impl->mouseDown;
}
std::uint32_t Host::Modifiers() const
{
    return impl->modifiers;
}
bool Host::PointerInside() const
{
    return impl->window && impl->mouseInside;
}
void Host::SetDoubleClickTime(std::uint32_t milliseconds)
{
    impl->doubleClickTime = milliseconds;
}
std::uint32_t Host::DoubleClickTime() const
{
    return impl->doubleClickTime;
}
void Host::SuspendFocusEvents(bool suspended)
{
    if (impl->focusSuspended == suspended)
        return;
    impl->focusSuspended = suspended;
    impl->ApplyPointer();
    if (suspended)
    {
        impl->mouseDown = false;
        impl->ResetKeys();
        impl->MouseInside(false);
    }
    else
        SDL_FilterEvents(
            [](void *window, SDL_Event *event) {
                return (event->type != SDL_EVENT_WINDOW_FOCUS_GAINED && event->type != SDL_EVENT_WINDOW_FOCUS_LOST) ||
                       event->window.windowID != SDL_GetWindowID(static_cast<SDL_Window *>(window));
            },
            impl->window);
}
void Host::FlushInput(bool mouse, bool keys)
{
    if (mouse)
        SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
    if (keys)
    {
        SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_TEXT_INPUT);
        impl->queuedText.clear();
        impl->heldKeys = {};
        impl->suppressRepeatedText = false;
    }
}
void Host::SetSlowKeyRepeat(bool slow)
{
    impl->slowKeyRepeat = slow;
    impl->suppressRepeatedText = false;
}
bool Host::SlowKeyRepeat() const
{
    return impl->slowKeyRepeat;
}
bool Host::PushPointer(float x, float y, bool down, std::uint32_t button)
{
    // Automation uses logical Studio coordinates. Re-enter the same window
    // coordinate path as physical input, including scaling and letterboxing.
    if (!impl->renderer ||
        !SDL_RenderCoordinatesToWindow(impl->renderer, x - impl->source.x, y - impl->source.y, &x, &y))
        return false;
    SDL_Event event = {};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = SDL_GetWindowID(impl->window);
    event.button.button = static_cast<Uint8>(button);
    event.button.down = down;
    event.button.x = x;
    event.button.y = y;
    return SDL_PushEvent(&event);
}
bool Host::PushKey(std::uint32_t key, bool down, std::uint32_t modifiers)
{
    SDL_Event event = {};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = SDL_GetWindowID(impl->window);
    for (int scan = 0; scan < SDL_SCANCODE_COUNT; ++scan)
        if (VirtualKey(static_cast<SDL_Scancode>(scan)) == key)
        {
            event.key.scancode = static_cast<SDL_Scancode>(scan);
            break;
        }
    event.key.down = down;
    event.key.mod =
        static_cast<SDL_Keymod>(((modifiers & shift) ? SDL_KMOD_LSHIFT : 0) |
                                ((modifiers & control) ? SDL_KMOD_LCTRL : 0) | ((modifiers & alt) ? SDL_KMOD_LALT : 0));
    return SDL_PushEvent(&event);
}
bool Host::PushText(const std::string &utf8)
{
    if (!impl->window || utf8.empty() || utf8.find('\0') != std::string::npos)
        return SDL_SetError("Invalid SDL text input");
    try
    {
        impl->queuedText.push_back(utf8);
        SDL_Event event = {};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = SDL_GetWindowID(impl->window);
        event.text.text = impl->queuedText.back().c_str();
        if (SDL_PushEvent(&event))
            return true;
        impl->queuedText.pop_back();
        return false;
    }
    catch (const std::bad_alloc &)
    {
        return SDL_SetError("Cannot allocate SDL text input");
    }
}
bool Host::GetClipboardText(std::string &utf8) const
{
    utf8.clear();
    if (!impl->window)
        return false;
    std::unique_ptr<char, decltype(&SDL_free)> text(SDL_GetClipboardText(), SDL_free);
    if (!text)
        return false;
    try
    {
        utf8 = text.get();
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    return true;
}
bool Host::SetClipboardText(const std::string &utf8)
{
    if (!impl->window || utf8.find('\0') != std::string::npos || !SDL_SetClipboardText(utf8.c_str()))
        return false;
    impl->clipboardOwned = true;
    return true;
}
bool Host::OwnsClipboard() const
{
    return impl->clipboardOwned;
}
std::uint64_t Host::CreateCursor(const std::uint8_t *andBits, const std::uint8_t *xorBits, int width, int height,
                                 int pitch, int hotX, int hotY)
{
    if (!impl->window || !andBits || !xorBits || width < 8 || width > 512 || width % 8 || height < 1 || height > 512 ||
        pitch < width / 8 || hotX < 0 || hotY < 0 || hotX >= width || hotY >= height || !nextCursor)
        return 0;
    try
    {
        Impl::CursorImage image = {width, height, hotX, hotY, {}, {}};
        image.andBits.resize(width / 8 * height);
        image.xorBits.resize(width / 8 * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width / 8; ++x)
            {
                image.andBits[y * (width / 8) + x] = andBits[std::size_t(y) * pitch + x];
                image.xorBits[y * (width / 8) + x] = xorBits[std::size_t(y) * pitch + x];
            }
        const auto id = nextCursor;
        impl->cursors.emplace(id, std::move(image));
        ++impl->createdCursors;
        ++nextCursor;
        return id;
    }
    catch (const std::bad_alloc &)
    {
        return 0;
    }
}
void Host::DestroyCursor(std::uint64_t cursor)
{
    const auto found = impl->cursors.find(cursor);
    if (found == impl->cursors.end())
        return;
    if (impl->currentCursor == cursor)
    {
        SetSystemCursor(false);
        impl->currentCursor = 0;
    }
    impl->cursors.erase(found);
}
bool Host::SetCursor(std::uint64_t cursor)
{
    const auto found = impl->cursors.find(cursor);
    if (!impl->window || found == impl->cursors.end())
        return false;
    if (!impl->triedTransparentCursor)
    {
        impl->triedTransparentCursor = true;
        const std::uint8_t empty[128] = {};
        impl->transparentCursor = SDL_CreateCursor(empty, empty, 32, 32, 0, 0);
    }
    impl->currentCursor = cursor;
    impl->ApplyCursor();
    ++impl->selectedCursors;
    return true;
}
bool Host::SetSystemCursor(bool waiting)
{
    if (!impl->window)
        return false;
    impl->currentCursor = 0;
    if (waiting && !impl->waitCursor)
        impl->waitCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    if ((waiting && !impl->waitCursor) || !SDL_SetCursor(waiting ? impl->waitCursor : SDL_GetDefaultCursor()))
        return false;
    impl->currentCursor = 0;
    impl->cursorVisible ? SDL_ShowCursor() : SDL_HideCursor();
    return true;
}
bool Host::SetCursorVisible(bool visible)
{
    if (!impl->window)
        return false;
    impl->cursorVisible = visible;
    if (impl->currentCursor)
    {
        impl->ApplyCursor();
        return true;
    }
    return visible ? SDL_ShowCursor() : SDL_HideCursor();
}
bool Host::WarpPointerGlobal(float x, float y)
{
    return impl->window && SDL_WarpMouseGlobal(x, y);
}
bool Host::WarpPointer(float x, float y)
{
    float windowX, windowY;
    if (!impl->renderer ||
        !SDL_RenderCoordinatesToWindow(impl->renderer, x - impl->source.x, y - impl->source.y, &windowX, &windowY))
        return false;
    SDL_WarpMouseInWindow(impl->window, windowX, windowY);
    // Tracking may read the pointer before SDL delivers the generated motion.
    impl->mouseX = x;
    impl->mouseY = y;
    impl->MouseInside(x >= 0 && x < impl->width && y >= 0 && y < impl->height);
    return true;
}
bool Host::CapturePointer(bool capture)
{
    if (!impl->window)
        return false;
    impl->captureRequested = capture;
    return impl->ApplyPointer();
}
bool Host::ConfinePointer(bool confined)
{
    if (!impl->window)
        return false;
    impl->confinementRequested = confined;
    return impl->ApplyPointer();
}
void Host::SuspendPointer(bool suspended)
{
    impl->pointerSuspended = suspended;
    impl->ApplyPointer();
}
PointerStatus Host::Pointer() const
{
    return {impl->captureRequested, impl->window && (SDL_GetWindowFlags(impl->window) & SDL_WINDOW_MOUSE_CAPTURE) != 0,
            impl->confinementRequested, impl->window && SDL_GetWindowMouseGrab(impl->window),
            impl->pointerSuspended || impl->focusSuspended};
}
CursorStatus Host::Cursors() const
{
    return {impl->createdCursors, impl->selectedCursors, impl->currentCursor, impl->cursors.size(),
            impl->window && impl->cursorVisible};
}
void Host::Invalidate(const KauaiPixels::DamageRect *rectangle)
{
    impl->damage.Invalidate(rectangle);
}
void Host::Validate(const KauaiPixels::DamageRect *rectangle)
{
    impl->damage.Validate(rectangle);
}
bool Host::DamageBounds(KauaiPixels::DamageRect &rectangle) const
{
    return impl->damage.Bounds(rectangle);
}
bool Host::TakeDamage(std::vector<KauaiPixels::DamageRect> &rectangles)
{
    return impl->damage.Take(rectangles);
}
bool Host::ScrollDamage(const KauaiPixels::DamageRect &rectangle, std::int32_t dx, std::int32_t dy)
{
    if (impl->damage.Scroll(rectangle, dx, dy))
        return true;
    impl->damage.Invalidate(&rectangle); // Preserve repaint on allocation failure.
    return false;
}
} // namespace KauaiSDL
