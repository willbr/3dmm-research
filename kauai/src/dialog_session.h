/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_DIALOG_SESSION_H
#define KAUAI_DIALOG_SESSION_H
#include "dialog_resource.h"
namespace KauaiDialogs
{
enum class AlertButtons
{
    ok,
    okCancel,
    yesNo,
    yesNoCancel
};
enum class AlertResult
{
    yes,
    no,
    cancel
};
enum class AssertionResult : std::int32_t
{
    ignore = 0,
    debugger = 1,
    quit = 2
};
struct Value
{
    std::int32_t number = 0;
    std::string text;
    std::vector<std::string> choices;
};
// Active control values are separate from Dialog's stored values, matching the
// original native controls. Callbacks decide when to read/write the model.
struct Session
{
    Resource resource;
    std::vector<Binding> bindings;
    std::vector<Value> values;
    std::int32_t focus = -1, result = -1;
    std::size_t selectedTab = 0;
    bool closed = false, quitRequested = false;
    // Zero disables Escape/window-close cancellation (e.g. a Yes/No alert).
    std::uint32_t cancelControl = 2;
    void *context = nullptr;
    bool (*changed)(void *, std::int32_t &) = nullptr;
    bool Init(const Resource &source);
    bool InitAlert(const std::string &text, const std::string &title, AlertButtons buttons);
    AlertResult AlertAnswer() const;
    bool InitAssertion(const std::string &text);
    AssertionResult AssertionAnswer() const;
    std::int32_t Item(std::uint32_t control) const;
    void Notify(std::int32_t item);
    void Cancel();
};
std::string Caption(const std::u16string &units, bool mnemonics = false);
} // namespace KauaiDialogs
#endif
