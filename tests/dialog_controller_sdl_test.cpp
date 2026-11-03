/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_dialog.h"
#include <chrono>
#include <cstdio>
#include <atomic>
#include <thread>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
struct Test
{
    int stage = 0, vetoes = 0, ticks = 0;
    bool failed = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    bool Send(KauaiSDL::DialogActionKind kind, std::uint32_t control, std::string text = {})
    {
        return KauaiSDL::QueueDialogAction(KauaiSDL::CurrentDialog().generation, {kind, control, std::move(text)});
    }
};
static bool Changed(PDialog dialog, LegacyLong *item, void *context)
{
    using KauaiSDL::DialogActionKind;
    auto &test = *static_cast<Test *>(context);
    if (std::chrono::steady_clock::now() - test.start > std::chrono::seconds(8))
    {
        std::fprintf(stderr, "SDL dialog controller timeout at stage %d\n", test.stage);
        test.failed = true;
        *item = ivNil;
        return true;
    }
    if (*item == ivNil)
    {
        ++test.ticks;
        const auto state = KauaiSDL::CurrentDialog();
        if (!state.ready)
            return false;
        if (test.stage == 0)
        {
            test.failed |= !test.Send(DialogActionKind::click, 202);
            test.stage = 1;
        }
        if (test.stage == 4 && test.vetoes == 1)
        {
            for (const auto &control : state.controls)
                if (control.id == 202 && control.number == 0)
                {
                    test.failed |= !test.Send(DialogActionKind::click, 201);
                    test.stage = 5;
                    break;
                }
        }
        return false;
    }
    test.failed |= !dialog->FGetValues(0, dialog->IvMac());
    if (*item == 1 && test.stage == 1)
    {
        test.failed |= !dialog->FGetCheck(1);
        test.failed |= !test.Send(DialogActionKind::click, 204);
        test.stage = 2;
    }
    else if (*item == 2 && test.stage == 2)
    {
        test.failed |= dialog->LwGetRadio(2) != 1;
        test.failed |= !test.Send(DialogActionKind::text, 205, "Caf\xC3\xA9");
        test.stage = 3;
    }
    else if (*item == 3 && test.stage == 3)
    {
        String value;
        dialog->GetStn(3, &value);
        if (value.Cch() == 4 && static_cast<byte>(value.Prgch()[3]) == 0xE9)
        {
            test.failed |= !test.Send(DialogActionKind::click, 201);
            test.stage = 4;
        }
    }
    else if (*item == 0 && test.stage == 4)
    {
        ++test.vetoes;
        dialog->PutCheck(1, false);
        dialog->SetValues(1, 2);
        dialog->SelectDit(3);
    }
    else if (*item == 0 && test.stage == 5)
    {
        *item = 7;
        return true; // Original callbacks may override the dismissal result.
    }
    return false;
}
int main()
{
    vwig.hinst = GetModuleHandle(nullptr);
    Test test;
    auto dialog = Dialog::PdlgNew(101, Changed, &test);
    if (!dialog || dialog->IvMac() != 5)
        return 1;
    String text;
    text.SetSz(PszLit("Initial"));
    if (!dialog->FPutStn(3, &text) || !dialog->FPutStn(4, &text))
        return 1;
    text.SetSz(PszLit("Choice"));
    if (!dialog->FAddToList(4, &text))
        return 1;
    const auto result = dialog->IditDo(3);
    dialog->GetStn(3, &text);
    const bool passed = result == 7 && !test.failed && test.stage == 5 && test.vetoes == 1 && test.ticks > 0 &&
                        !dialog->FGetCheck(1) && dialog->LwGetRadio(2) == 1 && text.Cch() == 4 &&
                        static_cast<byte>(text.Prgch()[3]) == 0xE9 && !KauaiSDL::CurrentDialog().active;
    ReleasePpo(&dialog);
    if (!passed)
        return 1;
    static ApplicationBase app; // Legacy application objects require zero-initialized storage.
    struct AlertCase
    {
        long buttons;
        long icon;
        bool escape;
        unsigned control;
        tribool expected;
    };
    for (const auto &example :
         {AlertCase{bkOk, cokInformation, true, 0, tYes}, AlertCase{bkOkCancel, cokExclamation, true, 0, tMaybe},
          AlertCase{bkYesNo, cokQuestion, true, 3, tNo}, AlertCase{bkYesNoCancel, cokStop, false, 2, tMaybe},
          AlertCase{bkYesNoCancel, cokNil, false, 1, tYes}})
    {
        std::atomic<bool> done{false}, failed{false};
        std::thread input([&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            bool sentEscape = false, sentClick = false;
            auto escapedAt = std::chrono::steady_clock::now();
            while (!done)
            {
                const auto state = KauaiSDL::CurrentDialog();
                if (state.ready)
                {
                    if (!sentEscape && !sentClick)
                    {
                        if (state.controls.empty() ||
                            state.controls[0].label != "Caf\xC3\xA9 & original alert\nSecond line")
                            failed = true;
                        if (!KauaiSDL::QueueDialogAction(
                                state.generation, {KauaiSDL::DialogActionKind::screenshot, 0, "dialog-alert.bmp"}))
                            failed = true;
                    }
                    if (example.escape && !sentEscape)
                    {
                        if (!KauaiSDL::QueueDialogAction(state.generation, {KauaiSDL::DialogActionKind::escape, 0, {}}))
                            failed = true;
                        sentEscape = true;
                        escapedAt = std::chrono::steady_clock::now();
                    }
                    if (example.control && !sentClick &&
                        (!example.escape ||
                         std::chrono::steady_clock::now() - escapedAt > std::chrono::milliseconds(80)))
                    {
                        if (!KauaiSDL::QueueDialogAction(state.generation,
                                                         {KauaiSDL::DialogActionKind::click, example.control, {}}))
                            failed = true;
                        sentClick = true;
                    }
                }
                if (std::chrono::steady_clock::now() > deadline)
                {
                    std::fprintf(stderr, "Original SDL alert timed out.\n");
                    std::abort(); // Never leave a failed modal test hanging.
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (!sentEscape && !sentClick)
                failed = true;
        });
        const auto answer =
            app.TGiveAlertSz(PszLit("Caf\xE9 & original alert\nSecond line"), example.buttons, example.icon);
        done = true;
        input.join();
        if (failed || answer != example.expected || KauaiSDL::CurrentDialog().active)
            return 1;
    }
    std::puts(
        "Original Dialog through SDL: real control input, legacy value reads/writes, UTF-8/legacy conversion, "
        "callback veto/refocus, timer callbacks, overridden dismissal result and original application alerts passed.");
}
