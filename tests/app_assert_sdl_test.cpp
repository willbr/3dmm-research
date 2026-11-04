/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_dialog.h"
#include "sdl_host.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
#ifndef DEBUG
int main()
{
    return 77;
}
#else
class AssertApp : public ApplicationBase
{
  public:
    bool Open()
    {
        return _FInitSDLWindow("SDL assertion test", 640, 480);
    }
    void Close()
    {
        _CloseSDLWindow();
    }
    LegacyLong Model()
    {
        String message = PszLit("SDL assertion regression: A & B"), data = PszLit("12345678"),
               stack = PszLit("Test stack");
        return _TmcSDLAssert(&message, &data, &stack);
    }
};
#define CHECK(v)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(v))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL assertion failed at %d: %s\n", __LINE__, #v);                                    \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
struct Input
{
    KauaiSDL::DialogActionKind kind = KauaiSDL::DialogActionKind::click;
    std::uint32_t control = 1;
    const char *screenshot = nullptr;
    bool valid = false;
    void Run()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto state = KauaiSDL::CurrentDialog();
            if (!state.active || !state.ready)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            bool ignore = false, debugger = false, quit = false, message = false, data = false;
            for (const auto &c : state.controls)
            {
                ignore |= c.id == 1 && c.label == "Ignore";
                debugger |= c.id == 3 && c.label == "Debugger";
                quit |= c.id == 2 && c.label == "Quit";
                message |= c.label.find("SDL assertion regression: A & B") != std::string::npos;
                data |= c.label.find("12345678") != std::string::npos;
            }
            valid = state.title == "Assertion Failure" && ignore && debugger && quit && message && data;
            if (screenshot)
            {
                KauaiSDL::DialogAction capture;
                capture.kind = KauaiSDL::DialogActionKind::screenshot;
                capture.text = screenshot;
                valid &= KauaiSDL::QueueDialogAction(state.generation, capture);
            }
            KauaiSDL::DialogAction action;
            action.kind = kind;
            action.control = control;
            valid &= KauaiSDL::QueueDialogAction(state.generation, action);
            return;
        }
    }
};
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    _putenv_s("KAUAI_FONT_PROVIDER", "freetype");
    auto app = NewObj AssertApp;
    CHECK(app && app->Open());
#ifdef WIN
    vwig.lwThreadMain = LwThreadCur();
#endif
    std::remove(argv[1]);
    for (unsigned control : {1u, 3u})
    {
        Input input;
        input.control = control;
        input.screenshot = control == 1 ? argv[1] : nullptr;
        std::thread drive([&] { input.Run(); });
        std::uint32_t data = 0x12345678;
        const bool debug = app->FAssertProcApp((schar *)"app_assert_sdl_test.cpp", 77,
                                               (schar *)"SDL assertion regression: A & B", &data, 4);
        drive.join();
        CHECK(input.valid && debug == (control == 3));
        CHECK(!KauaiSDL::CurrentDialog().active);
    }
    for (auto kind :
         {KauaiSDL::DialogActionKind::click, KauaiSDL::DialogActionKind::enter, KauaiSDL::DialogActionKind::escape})
    {
        Input input;
        input.kind = kind;
        input.control = 2;
        std::thread drive([&] { input.Run(); });
        const auto choice = app->Model(); // Observe Quit without terminating the test process.
        drive.join();
        CHECK(input.valid && choice == (kind == KauaiSDL::DialogActionKind::enter ? 0 : 2));
        CHECK(!KauaiSDL::CurrentDialog().active);
    }
    auto screenshot = std::fopen(argv[1], "rb");
    CHECK(screenshot);
    char signature[2] = {};
    CHECK(std::fread(signature, 1, 2, screenshot) == 2);
    std::fclose(screenshot);
    CHECK(signature[0] == 'B' && signature[1] == 'M');
    app->Close();
    ReleasePpo(&app);
    std::puts("SDL assertion UI: actual framework Ignore/Debugger, preserved diagnostic bytes, Quit model, default "
              "Enter, legacy Escape/Quit, screenshot and modal cleanup passed.");
}
#endif
