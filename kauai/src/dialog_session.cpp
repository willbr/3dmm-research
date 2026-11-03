/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dialog_session.h"
#include "text_utf8.h"
#include <new>
#include <stdexcept>
#include <utility>
namespace KauaiDialogs
{
bool Session::Init(const Resource &source)
{
    try
    {
        if (source.bounds.width <= 0 || source.bounds.height <= 0)
            return false;
        if (source.menu.ordinal || !source.menu.text.empty() || source.windowClass.ordinal ||
            !source.windowClass.text.empty() || (source.style & 0x40000000))
            return false;
        // Reject unknown renderers before showing a window; preserve the
        // legacy adapter for custom classes/resource images until supported.
        for (const auto &control : source.controls)
        {
            if (!control.windowClass.ordinal || control.title.ordinal || !control.creationData.empty())
                return false;
            switch (control.windowClass.number)
            {
            case 0x80:
                switch (control.style & 15)
                {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 7:
                case 9:
                    break;
                default:
                    return false;
                }
                break;
            case 0x81:
                // Native case conversion, OEM conversion and numeric filtering
                // need their own policy before those edit styles can move.
                if (control.style & (8 | 16 | 0x400 | 0x2000))
                    return false;
                break;
            case 0x85:
                if ((control.style & 3) < 2 || (control.style & (0x10 | 0x20 | 0x100)))
                    return false;
                break;
            case 0x82:
                if ((control.style & 31) > 2)
                    return false;
                break;
            default:
                return false;
            }
        }
        Session loaded;
        loaded.resource = source;
        if (!BuildBindings(source, loaded.bindings) || loaded.bindings.empty())
            return false;
        loaded.values.resize(loaded.bindings.size());
        *this = std::move(loaded);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    catch (const std::length_error &)
    {
        return false;
    }
}
std::int32_t Session::Item(std::uint32_t control) const
{
    for (std::size_t i = bindings.size(); i-- > 0;)
        if (control >= bindings[i].first && control < bindings[i].limit)
            return static_cast<std::int32_t>(i);
    return -1;
}
bool Session::InitAlert(const std::string &text, const std::string &title, AlertButtons buttons)
{
    try
    {
        if (buttons != AlertButtons::ok && buttons != AlertButtons::okCancel && buttons != AlertButtons::yesNo &&
            buttons != AlertButtons::yesNoCancel)
            return false;
        std::vector<std::uint16_t> words, caption;
        if (!KauaiText::DecodeUtf8(text, words) || !KauaiText::DecodeUtf8(title, caption))
            return false;
        Resource source;
        source.title.assign(caption.begin(), caption.end());
        // Conservative wrapping budget, including wide glyphs. The frontend
        // wraps the actual text and scrolls the resource canvas on small screens.
        std::size_t rows = 1, column = 0;
        for (auto unit : words)
        {
            if (unit == '\r')
                continue;
            if (unit == '\n')
            {
                ++rows;
                column = 0;
            }
            else if (++column == 40)
            {
                ++rows;
                column = 0;
            }
        }
        if (rows > 3000)
            return false;
        const auto textHeight = static_cast<std::int16_t>(rows * 9 + 4);
        source.bounds = {0, 0, 300, static_cast<std::int16_t>(textHeight + 40)};
        Control body;
        body.id = 100;
        body.windowClass.ordinal = true;
        body.windowClass.number = 0x82;
        body.style = 0x50000000;
        body.bounds = {8, 8, 284, textHeight};
        // Static captions use mnemonic escaping; alert text is literal.
        for (auto unit : words)
        {
            body.title.text.push_back(static_cast<char16_t>(unit));
            if (unit == '&')
                body.title.text.push_back(u'&');
        }
        source.controls.push_back(std::move(body));
        const bool yesNo = buttons == AlertButtons::yesNo || buttons == AlertButtons::yesNoCancel;
        const bool cancel = buttons == AlertButtons::okCancel || buttons == AlertButtons::yesNoCancel;
        const int count = 1 + static_cast<int>(yesNo) + static_cast<int>(cancel);
        int x = 292 - count * 58;
        const auto add = [&](std::uint32_t id, const char16_t *label, bool primary) {
            Control button;
            button.id = id;
            button.windowClass.ordinal = true;
            button.windowClass.number = 0x80;
            button.style = 0x50010000 | static_cast<unsigned>(primary);
            button.bounds = {static_cast<std::int16_t>(x), static_cast<std::int16_t>(textHeight + 18), 52, 14};
            button.title.text = label;
            source.controls.push_back(std::move(button));
            x += 58;
        };
        add(1, yesNo ? u"Yes" : u"OK", true);
        if (yesNo)
            add(3, u"No", false);
        if (cancel)
            add(2, u"Cancel", false);
        Session loaded;
        if (!loaded.Init(source))
            return false;
        loaded.cancelControl = cancel ? 2 : yesNo ? 0 : 1;
        *this = std::move(loaded);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
    catch (const std::length_error &)
    {
        return false;
    }
}
bool Session::InitAssertion(const std::string &text)
{
    try
    {
        Session loaded;
        if (!loaded.InitAlert(text, "Assertion Failure", AlertButtons::yesNoCancel))
            return false;
        for (auto &control : loaded.resource.controls)
            switch (control.id)
            {
            case 1:
                control.title.text = u"&Ignore";
                break;
            case 3:
                control.title.text = u"&Debugger";
                break;
            case 2:
                control.title.text = u"&Quit";
                break;
            }
        // Match the original assert dialog's default Ignore and IDCANCEL/Quit.
        loaded.cancelControl = 2;
        *this = std::move(loaded);
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}
AssertionResult Session::AssertionAnswer() const
{
    if (!closed || result < 0)
        return AssertionResult::ignore;
    return result == Item(3)   ? AssertionResult::debugger
           : result == Item(2) ? AssertionResult::quit
                               : AssertionResult::ignore;
}
AlertResult Session::AlertAnswer() const
{
    if (!closed || result < 0)
        return AlertResult::cancel;
    if (result == Item(1))
        return AlertResult::yes;
    if (result == Item(3))
        return AlertResult::no;
    return AlertResult::cancel;
}
void Session::Notify(std::int32_t item)
{
    if (closed)
        return;
    const bool dismiss = changed ? changed(context, item)
                                 : item >= 0 && static_cast<std::size_t>(item) < bindings.size() &&
                                       bindings[item].kind == ItemKind::button;
    if (dismiss)
    {
        closed = true;
        result = item;
    }
}
void Session::Cancel()
{
    if (!cancelControl)
        return;
    const auto item = Item(cancelControl); // IDCANCEL by default, including Product Info's original OK button.
    if (item >= 0 && bindings[item].kind == ItemKind::button)
        Notify(item);
    else
    {
        closed = true;
        result = -1;
    }
}
std::string Caption(const std::u16string &units, bool mnemonics)
{
    std::string result;
    std::vector<std::uint16_t> words;
    for (std::size_t i = 0; i < units.size(); ++i)
    {
        if (mnemonics && units[i] == u'&')
        {
            if (i + 1 == units.size() || units[i + 1] != u'&')
                continue;
            ++i;
        }
        if (units[i] != u'\r')
            words.push_back(units[i]);
    }
    if (!KauaiText::EncodeUtf8(words, result))
        return {};
    return result;
}
} // namespace KauaiDialogs
