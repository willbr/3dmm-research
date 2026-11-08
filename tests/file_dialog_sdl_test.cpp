/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_file_dialog.h"
#include "sdl_dialog.h"
#include "platform_files.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace KauaiSDL;
struct Test
{
    FileDialog *dialog;
    std::string root, screenshot;
    int stage = 0;
    SDL_Window *parent = nullptr;
    bool failed = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool Send(DialogActionKind action, unsigned control = 0, const std::string &text = {})
    {
        return QueueDialogAction(CurrentDialog().generation, {action, control, text});
    }
    static bool Preview(void *, const std::string &, DialogImage &image)
    {
        image.width = 144;
        image.height = 80;
        image.pixels.assign(144 * 80, 0xFF204080);
        return true;
    }
    static void Idle(void *context)
    {
        auto &test = *static_cast<Test *>(context);
        // Software rendering under CPU emulation needs a larger wall-clock
        // allowance; each stage still requires the real input/state transition.
        if (std::chrono::steady_clock::now() - test.start > std::chrono::seconds(30))
        {
            std::fprintf(stderr, "SDL file dialog timeout at %d: directory='%s', name='%s', error='%s'\n", test.stage,
                         test.dialog->browser.directory.c_str(), test.dialog->browser.name.c_str(),
                         test.dialog->browser.error.c_str());
            for (const auto &control : CurrentDialog().controls)
                if (control.id == 10)
                    std::fprintf(stderr, "Folder field: '%s'\n", control.text.c_str());
            std::exit(1);
        }
        const auto state = CurrentDialog();
        if (!state.ready)
            return;
        test.failed |= !state.embedded || state.window != SDL_GetWindowID(test.parent);
        int windows = 0;
        SDL_free(SDL_GetWindows(&windows));
        test.failed |= windows != 1;
        const auto control = [&](unsigned id) -> DialogControl {
            for (const auto &item : state.controls)
                if (item.id == id)
                    return item;
            return {};
        };
        auto &browser = test.dialog->browser;
        if (control(20).number != 1)
        {
            std::fprintf(stderr, "Portfolio artwork missing: %s\n", SDL_GetError());
            test.failed = true;
        }
        switch (test.stage)
        {
        case 0:
            test.failed |= !test.Send(DialogActionKind::text, 10, test.root + "/sub");
            ++test.stage;
            break;
        case 1:
            if (control(10).text != test.root + "/sub")
                break;
            test.failed |= !test.Send(DialogActionKind::enter);
            ++test.stage;
            break;
        case 2:
            if (browser.directory.find("sub") == std::string::npos)
                break;
            test.failed |= !browser.error.empty(); // Enter navigates without trying to accept an empty filename.
            test.failed |= !test.Send(DialogActionKind::click, 13);
            ++test.stage;
            break;
        case 3:
            // Choose the real current-folder ancestor from the navigation
            // tree, rather than its duplicate in the common places section.
            for (const auto &place : state.controls)
                if (place.id >= 0x100000 && place.text == browser.home && place.label != "My movies")
                {
                    test.failed |= !test.Send(DialogActionKind::click, place.id);
                    ++test.stage;
                    break;
                }
            break;
        case 4:
            if (browser.entries.size() != 2)
                break;
            test.failed |= !test.Send(DialogActionKind::click, 5); // Back to sub.
            ++test.stage;
            break;
        case 5:
            if (browser.directory.find("sub") == std::string::npos)
                break;
            test.failed |= !test.Send(DialogActionKind::click, 4); // Up to fixture.
            ++test.stage;
            break;
        case 6:
            if (browser.entries.size() != 2)
                break;
            test.failed |= !test.Send(DialogActionKind::click, 1001);
            ++test.stage;
            break;
        case 7:
            if (control(21).number != 1)
                break;
            test.failed |= !test.Send(DialogActionKind::screenshot, 0, test.screenshot);
            ++test.stage;
            break;
        case 8:
            test.failed |= !test.Send(DialogActionKind::click, 1);
            ++test.stage;
            break;
        }
    }
};
static void CheckAt(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "SDL file dialog check failed at line %d.\n", line);
        std::exit(1);
    }
}
#define Check(value) CheckAt(bool(value), __LINE__)
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    const std::string root = argv[1];
    KauaiFiles::MakeDirectory(root.c_str());
    Check(KauaiFiles::MakeDirectory((root + "/sub").c_str()));
    KauaiFiles::OpenOptions options;
    options.write = true;
    options.creation = KauaiFiles::Creation::CreateAlways;
    auto file = KauaiFiles::Open((root + "/fixture.3mm").c_str(), options);
    Check(file);
    KauaiFiles::Close(file);
    Check(SDL_InitSubSystem(SDL_INIT_VIDEO));
    auto parent = SDL_CreateWindow("Portfolio parent", 640, 480, SDL_WINDOW_HIDDEN);
    Check(parent);
    auto renderer = SDL_CreateRenderer(parent, nullptr);
    Check(renderer);
    for (int variant = 0; variant < 3; ++variant)
    {
        const int width = variant == 0 ? 640 : variant == 1 ? 1500 : 1000;
        const int height = variant == 0 ? 480 : variant == 1 ? 1000 : 700;
        const auto mode = variant == 1 ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX;
        Check(SDL_SetWindowSize(parent, width, height));
        Check(SDL_SetRenderLogicalPresentation(renderer, 640, 480, mode));
        Check(SDL_SetRenderScale(renderer, 1.25f, 1.25f));
        SDL_Rect clip{3, 5, 90, 70};
        Check(SDL_SetRenderClipRect(renderer, &clip));
        const bool textActive = variant == 1;
        if (textActive)
        {
            Check(SDL_StartTextInput(parent));
            SDL_Rect area{17, 19, 40, 20};
            Check(SDL_SetTextInputArea(parent, &area, 7));
        }
        else
            Check(SDL_StopTextInput(parent));
        FileDialog dialog;
        dialog.title = "SDL portfolio test";
        dialog.browser.filters = {{"Movies", "*.3mm"}};
        Check(dialog.browser.Navigate(root));
        dialog.browser.home = dialog.browser.directory;
        dialog.artwork.width = 640;
        dialog.artwork.height = 480;
        dialog.artwork.pixels.assign(640 * 480, 0xFF102030);
        dialog.preview = Test::Preview;
        Test test{&dialog, root, variant == 0 ? argv[2] : std::string(argv[2]) + std::to_string(variant) + ".bmp"};
        test.parent = parent;
        Check(RunFileDialog(dialog, SDL_GetWindowID(parent), Test::Idle, &test) && dialog.accepted && test.stage == 9 &&
              !test.failed && dialog.browser.selection.find("fixture.3mm") != std::string::npos &&
              !CurrentDialog().active);
        Check(SDL_GetWindowFromID(SDL_GetWindowID(parent)) == parent && SDL_GetRenderer(parent) == renderer);
        Check(SDL_TextInputActive(parent) == textActive);
        if (textActive)
        {
            SDL_Rect area;
            int cursor;
            Check(SDL_GetTextInputArea(parent, &area, &cursor) && area.x == 17 && area.y == 19 && area.w == 40 &&
                  area.h == 20 && cursor == 7);
        }
        int logicalWidth = 0, logicalHeight = 0;
        SDL_RendererLogicalPresentation restored;
        Check(SDL_GetRenderLogicalPresentation(renderer, &logicalWidth, &logicalHeight, &restored) &&
              logicalWidth == 640 && logicalHeight == 480 && restored == mode);
        float scaleX = 0, scaleY = 0;
        Check(SDL_GetRenderScale(renderer, &scaleX, &scaleY) && scaleX == 1.25f && scaleY == 1.25f);
        SDL_Rect restoredClip;
        Check(SDL_RenderClipEnabled(renderer) && SDL_GetRenderClipRect(renderer, &restoredClip) &&
              restoredClip.x == clip.x && restoredClip.y == clip.y && restoredClip.w == clip.w &&
              restoredClip.h == clip.h);
        auto image = SDL_LoadBMP(test.screenshot.c_str());
        Check(image && image->w == width && image->h == height);
        const float scale = variant == 1 ? 2.f : std::min(width / 640.f, height / 480.f);
        const int x = int((width - 640 * scale) / 2), y = int((height - 480 * scale) / 2);
        Uint8 r, g, b, a;
        Check(SDL_ReadSurfacePixel(image, x + int(600 * scale), y + int(300 * scale), &r, &g, &b, &a) && r == 16 &&
              g == 32 && b == 48);
        Check(SDL_ReadSurfacePixel(image, x + int(100 * scale), y + int(280 * scale), &r, &g, &b, &a) && r == 32 &&
              g == 64 && b == 128);
        if (x > 0)
            Check(SDL_ReadSurfacePixel(image, 0, height / 2, &r, &g, &b, &a) && r == 0 && g == 0 && b == 0);
        SDL_DestroySurface(image);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(parent);
    Check(KauaiFiles::Delete((root + "/fixture.3mm").c_str()));
    Check(KauaiFiles::DeleteDirectory((root + "/sub").c_str()) && KauaiFiles::DeleteDirectory(root.c_str()));
    std::puts("SDL portfolio: shared parent window/renderer, fitted and integer scaling, folder navigation, "
              "selection, artwork/preview pixels and renderer state restoration passed.");
}
