/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_DIALOG_H
#define KAUAI_SDL_DIALOG_H
#include "dialog_session.h"
namespace KauaiSDL
{
struct DialogControl
{
    std::uint32_t id = 0;
    std::int32_t item = -1, number = 0;
    KauaiDialogs::ItemKind kind = KauaiDialogs::ItemKind::button;
    float x = 0, y = 0, width = 0, height = 0;
    bool enabled = true, readOnly = false;
    std::string label, text;
};
struct DialogState
{
    bool active = false, ready = false, embedded = false;
    std::uint64_t generation = 0;
    std::uint32_t window = 0;
    std::string title;
    std::vector<DialogControl> controls;
};
enum class DialogActionKind
{
    click,
    text,
    enter,
    escape,
    tab,
    screenshot
};
struct DialogAction
{
    DialogActionKind kind = DialogActionKind::click;
    std::uint32_t control = 0;
    std::string text;
};
// Thread-safe observation/injection for automation. Actions become SDL input
// events on the UI thread; they never write control values directly.
DialogState CurrentDialog();
bool QueueDialogAction(std::uint64_t generation, const DialogAction &action);
// True once a dialog was shown, including cancellation/render failure. False
// means initialization failed before showing UI. Callback idle ticks use -1.
bool RunDialog(KauaiDialogs::Session &session, std::uint32_t parent = 0, void (*idle)(void *) = nullptr,
               void *context = nullptr);
} // namespace KauaiSDL
#endif
