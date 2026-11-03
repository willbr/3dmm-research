/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_dialog.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <fstream>
using namespace KauaiDialogs;
using namespace KauaiSDL;
struct Test
{
    Session *session;
    int stage = 0, ticks = 0, changes = 0, vetoes = 0;
    bool failed = false;
    std::uint64_t generation = 0;
    std::string screenshot;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool Send(DialogActionKind kind, std::uint32_t control = 0, std::string text = {})
    {
        return QueueDialogAction(CurrentDialog().generation, {kind, control, std::move(text)});
    }
    static bool Changed(void *context, std::int32_t &item)
    {
        auto &test = *static_cast<Test *>(context);
        if (item == -1)
        {
            ++test.ticks;
            return false;
        }
        ++test.changes;
        if (item == test.session->Item(1))
        {
            ++test.vetoes;
            return false;
        }
        return item == test.session->Item(2);
    }
    static void Idle(void *context)
    {
        auto &test = *static_cast<Test *>(context);
        auto &s = *test.session;
        if (std::chrono::steady_clock::now() - test.start > std::chrono::seconds(8))
        {
            std::fprintf(stderr, "SDL dialog timeout at stage %d, changes %d\n", test.stage, test.changes);
            test.failed = true;
            s.closed = true;
            return;
        }
        const auto state = CurrentDialog();
        if (!state.ready)
            return;
        test.generation = state.generation;
        switch (test.stage)
        {
        case 0:
            test.failed |= !test.Send(DialogActionKind::screenshot, 0, test.screenshot);
            test.failed |= !test.Send(DialogActionKind::click, 10);
            ++test.stage;
            break;
        case 1:
            if (s.values[s.Item(10)].number != 1)
                break;
            test.failed |= !test.Send(DialogActionKind::click, 21);
            ++test.stage;
            break;
        case 2:
            if (s.values[s.Item(21)].number != 1)
                break;
            test.failed |= !test.Send(DialogActionKind::text, 30, "Caf\xC3\xA9");
            ++test.stage;
            break;
        case 3:
            if (s.values[s.Item(30)].text != "Caf\xC3\xA9")
                break;
            test.failed |= test.Send(DialogActionKind::click, 11);
            test.failed |= test.Send(DialogActionKind::text, 31, "changed");
            test.failed |= !test.Send(DialogActionKind::enter);
            ++test.stage;
            break;
        case 4:
            if (test.vetoes != 1)
                break;
            test.failed |= !test.Send(DialogActionKind::escape);
            ++test.stage;
            break;
        }
    }
};
struct ScrollTest
{
    Session *session;
    bool sent = false, clicked = false, failed = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    static void Idle(void *context)
    {
        auto &test = *static_cast<ScrollTest *>(context);
        if (std::chrono::steady_clock::now() - test.start > std::chrono::seconds(8))
        {
            test.failed = true;
            test.session->closed = true;
            return;
        }
        const auto state = CurrentDialog();
        if (!state.ready || state.controls.empty())
            return;
        auto window = SDL_GetWindowFromID(state.window);
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        SDL_Rect usable;
        if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable) || height > usable.h)
            test.failed = true;
        const auto &button = state.controls.front();
        if (!test.sent)
        {
            // The original dialog-unit layout is taller than the display.
            test.failed |= button.y < height;
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = state.window;
            motion.motion.x = 30;
            motion.motion.y = 30;
            SDL_PushEvent(&motion);
            SDL_Event wheel{};
            wheel.type = SDL_EVENT_MOUSE_WHEEL;
            wheel.wheel.windowID = state.window;
            wheel.wheel.y = -1000;
            wheel.wheel.mouse_x = 30;
            wheel.wheel.mouse_y = 30;
            SDL_PushEvent(&wheel);
            test.sent = true;
        }
        else if (!test.clicked && button.y >= 0 && button.y + button.height < height)
        {
            test.failed |= !QueueDialogAction(state.generation, {DialogActionKind::click, 1, {}});
            test.clicked = true;
        }
    }
};
struct AssertionTest
{
    Session *session;
    std::uint32_t control;
    bool sent = false, failed = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    static void Idle(void *context)
    {
        auto &test = *static_cast<AssertionTest *>(context);
        if (std::chrono::steady_clock::now() - test.start > std::chrono::seconds(8))
        {
            test.failed = true;
            test.session->closed = true;
            return;
        }
        const auto state = CurrentDialog();
        if (!state.ready || test.sent)
            return;
        bool ignore = false, debugger = false, quit = false;
        for (const auto &control : state.controls)
        {
            ignore |= control.id == 1 && control.label == "Ignore";
            debugger |= control.id == 3 && control.label == "Debugger";
            quit |= control.id == 2 && control.label == "Quit";
        }
        test.failed |= state.title != "Assertion Failure" || !ignore || !debugger || !quit;
        test.failed |= !QueueDialogAction(state.generation, {DialogActionKind::click, test.control, {}});
        test.sent = true;
    }
};
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    Resource resource;
    resource.bounds = {0, 0, 220, 180};
    resource.title = u"Original dialog controls";
    const auto add = [&](unsigned id, unsigned windowClass, unsigned style, const char16_t *title, int y) {
        Control control;
        control.id = id;
        control.windowClass.ordinal = true;
        control.windowClass.number = windowClass;
        control.style = 0x50000000 | style;
        control.title.text = title;
        control.bounds = {5, static_cast<std::int16_t>(y), 180, 12};
        resource.controls.push_back(control);
    };
    add(10, 0x80, 3, u"Enabled", 5);
    add(20, 0x80, 9 | 0x20000, u"First", 23);
    add(21, 0x80, 9, u"Second", 40);
    add(30, 0x81, 0, u"", 60);
    add(31, 0x81, 0x800, u"", 80);
    add(11, 0x80, 3 | 0x08000000, u"Disabled", 100);
    add(1, 0x80, 1, u"&Accept", 125);
    add(2, 0x80, 0, u"Cancel", 145);
    Session session;
    if (!session.Init(resource))
        return 1;
    session.values[session.Item(31)].text = "Read only";
    Test test{&session};
    test.screenshot = argv[1];
    session.context = &test;
    session.changed = Test::Changed;
    if (!RunDialog(session, 0, Test::Idle, &test) || test.failed || test.stage != 5 || test.ticks < 1 ||
        test.changes < 4 || session.result != session.Item(2) || session.values[session.Item(11)].number ||
        session.values[session.Item(31)].text != "Read only" || CurrentDialog().active ||
        QueueDialogAction(test.generation, {DialogActionKind::enter, 0, {}}))
        return 1;
    std::ifstream image(argv[1], std::ios::binary);
    char signature[2];
    image.read(signature, 2);
    if (!image || signature[0] != 'B' || signature[1] != 'M')
        return 1;
    Resource tall;
    tall.title = u"Scrollable original layout";
    tall.bounds = {0, 0, 220, 3000};
    auto button = resource.controls[6];
    button.bounds.y = 2960;
    tall.controls.push_back(button);
    Session scrolling;
    if (!scrolling.Init(tall))
        return 1;
    ScrollTest scroll{&scrolling};
    if (!RunDialog(scrolling, 0, ScrollTest::Idle, &scroll) || scroll.failed || !scroll.clicked ||
        scrolling.result != scrolling.Item(1))
    {
        std::fprintf(stderr, "Oversized dialog scrolling failed (wheel %d, click %d).\n", scroll.sent, scroll.clicked);
        return 1;
    }
    for (unsigned control : {1u, 3u, 2u})
    {
        Session assertion;
        if (!assertion.InitAssertion("File.cpp line 77: SDL assertion regression\n12345678"))
            return 1;
        AssertionTest test{&assertion, control};
        const auto expected = control == 1   ? AssertionResult::ignore
                              : control == 3 ? AssertionResult::debugger
                                             : AssertionResult::quit;
        if (!RunDialog(assertion, 0, AssertionTest::Idle, &test) || test.failed || !test.sent ||
            assertion.AssertionAnswer() != expected || CurrentDialog().active)
            return 1;
    }
    std::puts(
        "SDL dialogs: actual SDL pointer/text/key input, checkbox/radio/edit values, disabled/read-only "
        "controls, callback veto, idle ticks, cancel, screenshot, oversized scrolling and session cleanup passed.");
}
