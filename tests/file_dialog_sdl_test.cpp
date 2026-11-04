/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_file_dialog.h"
#include "sdl_dialog.h"
#include "platform_files.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace KauaiSDL;
struct Test
{
    FileDialog *dialog;
    std::string root, screenshot;
    int stage = 0;
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
        const auto control = [&](unsigned id) -> DialogControl {
            for (const auto &item : state.controls)
                if (item.id == id)
                    return item;
            return {};
        };
        auto &browser = test.dialog->browser;
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
            test.failed |= !test.Send(DialogActionKind::click, 4);
            ++test.stage;
            break;
        case 3:
            if (browser.entries.size() != 2)
                break;
            test.failed |= !test.Send(DialogActionKind::click, 1001);
            ++test.stage;
            break;
        case 4:
            if (control(21).number != 1)
                break;
            test.failed |= !test.Send(DialogActionKind::screenshot, 0, test.screenshot);
            ++test.stage;
            break;
        case 5:
            test.failed |= !test.Send(DialogActionKind::click, 1);
            ++test.stage;
            break;
        }
    }
};
static void Check(bool value)
{
    if (!value)
    {
        std::fputs("SDL file dialog check failed.\n", stderr);
        std::exit(1);
    }
}
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
    FileDialog dialog;
    dialog.title = "SDL portfolio test";
    dialog.browser.filters = {{"Movies", "*.3mm"}};
    Check(dialog.browser.Navigate(root));
    dialog.browser.home = dialog.browser.directory;
    dialog.artwork.width = 640;
    dialog.artwork.height = 480;
    dialog.artwork.pixels.assign(640 * 480, 0xFF102030);
    dialog.preview = Test::Preview;
    Test test{&dialog, root, argv[2]};
    Check(SDL_InitSubSystem(SDL_INIT_VIDEO));
    auto parent = SDL_CreateWindow("Portfolio parent", 640, 480, SDL_WINDOW_HIDDEN);
    Check(parent);
    Check(RunFileDialog(dialog, SDL_GetWindowID(parent), Test::Idle, &test) && dialog.accepted && test.stage == 6 &&
          !test.failed && dialog.browser.selection.find("fixture.3mm") != std::string::npos && !CurrentDialog().active);
    SDL_DestroyWindow(parent);
    auto image = SDL_LoadBMP(argv[2]);
    Check(image);
    Uint8 r, g, b, a;
    Check(SDL_ReadSurfacePixel(image, 600, 300, &r, &g, &b, &a) && r == 16 && g == 32 && b == 48);
    Check(SDL_ReadSurfacePixel(image, 100, 280, &r, &g, &b, &a) && r == 32 && g == 64 && b == 128);
    SDL_DestroySurface(image);
    Check(KauaiFiles::Delete((root + "/fixture.3mm").c_str()));
    Check(KauaiFiles::DeleteDirectory((root + "/sub").c_str()) && KauaiFiles::DeleteDirectory(root.c_str()));
    std::puts("SDL portfolio: actual folder text/Enter, Up navigation, file selection, rendered artwork/preview "
              "pixels, acceptance and cleanup passed.");
}
