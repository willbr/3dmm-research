/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include "sdl_dialog.h"
#include "platform_text.h"
#include "text_utf8.h"
#include <exception>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifndef _WIN32
#include <unwind.h>
#endif
#include "frame.h"
ASSERTNAME
namespace
{
bool TextToUtf8(PZString text, std::string &utf8)
{
#ifdef UNICODE
    const auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
#else
    const auto encoding = KauaiText::Encoding::WindowsAnsi;
#endif
    const auto bytes = CchSz(text) * sizeof(achar);
    const auto count = KauaiText::ToUtf16(text, bytes, encoding, nullptr, 0);
    std::vector<std::uint16_t> units(count);
    return (!bytes || count) && (!count || KauaiText::ToUtf16(text, bytes, encoding, units.data(), count)) &&
           KauaiText::EncodeUtf8(units, utf8);
}
} // namespace
#ifdef DEBUG
LegacyLong ApplicationBase::_TmcSDLAssert(PString message, PString details, PString stack)
{
    auto &host = KauaiSDL::Main();
    LegacyLong choice = -1;
    try
    {
        std::string text, part;
        if (!TextToUtf8(message->Psz(), text))
            return -1;
        for (auto extra : {details, stack})
            if (extra->Cch())
            {
                if (!TextToUtf8(extra->Psz(), part))
                    return -1;
                text += "\n\n";
                text += part;
            }
        KauaiDialogs::Session session;
        if (!session.InitAssertion(text))
            return -1;
        host.FlushInput(true, true);
        host.SuspendFocusEvents(true);
        if (KauaiSDL::RunDialog(session, host.WindowID(), [](void *) { KauaiSDL::Main().Present(); }) && session.closed)
            choice = static_cast<LegacyLong>(session.AssertionAnswer());
    }
    catch (const std::exception &)
    {
    }
    if (!KauaiSDL::CurrentDialog().active)
        host.ResumeAfterModal();
    return choice;
}
#endif
tribool ApplicationBase::TGiveAlertSz(PZString psz, long bk, long cok)
{
    AssertThis(0);
    AssertSz(psz);
    using namespace KauaiDialogs;
    auto &host = KauaiSDL::Main();
    AlertResult answer = AlertResult::cancel;
    try
    {
        std::string text;
        if (!TextToUtf8(psz, text))
            return tMaybe;
        AlertButtons buttons = AlertButtons::ok;
        switch (bk)
        {
        case bkOk:
            break;
        case bkOkCancel:
            buttons = AlertButtons::okCancel;
            break;
        case bkYesNo:
            buttons = AlertButtons::yesNo;
            break;
        case bkYesNoCancel:
            buttons = AlertButtons::yesNoCancel;
            break;
        default:
            BugVar("bad bk value", &bk);
            break;
        }
        const char *title = "";
        switch (cok)
        {
        case cokNil:
            break;
        case cokInformation:
            title = "Information";
            break;
        case cokQuestion:
            title = "Question";
            break;
        case cokExclamation:
            title = "Warning";
            break;
        case cokStop:
            title = "Error";
            break;
        default:
            BugVar("bad cok value", &cok);
            break;
        }
        Session session;
        if (!session.InitAlert(text, title, buttons))
            return tMaybe;
        host.FlushInput(true, true);
        host.SuspendFocusEvents(true);
        if (KauaiSDL::RunDialog(session, host.WindowID(), [](void *) { KauaiSDL::Main().Present(); }))
            answer = session.AlertAnswer();
    }
    catch (const std::exception &)
    {
    }
    if (!KauaiSDL::CurrentDialog().active)
        host.ResumeAfterModal();
    return answer == AlertResult::yes ? tYes : answer == AlertResult::no ? tNo : tMaybe;
}
bool ApplicationBase::PumpHostEvent()
{
    EVT event;
    if (!KauaiSDL::Main().Active() || !_hostEvents.Next(KauaiSDL::Main(), event))
        return false;
    _DispatchEvt(&event);
    return true;
}
bool ApplicationBase::_FGetNextEvt(PEVT event)
{
    return _hostEvents.Next(KauaiSDL::Main(), *event, true);
}
void ApplicationBase::_DispatchEvt(PEVT event)
{
    using K = KauaiInput::Kind;
    switch (event->kind)
    {
    case K::focus:
        if (FForeground() != event->active)
            _Activate(event->active);
        break;
    case K::quit:
        if (vpcex)
            vpcex->EnqueueCid(cidQuit);
        break;
    case K::clipboard:
        if (!event->active)
            vpclip->Import();
        break;
    case K::exposed:
    case K::resized:
        KauaiSDL::Main().Invalidate();
        break;
    case K::buttonDown:
        ResetToolTip();
        if (event->button == 1)
            _HostMouseDown(*event);
        break;
    case K::buttonUp:
    case K::keyUp:
        ResetToolTip();
        break;
    case K::keyDown:
    case K::character:
        if (!_FDispatchWindowShortcut(*event) && !_FDispatchShortcut(*event))
        {
            CMD_KEY key;
            if (_FTranslateKeyEvt(event, &key) && vpcex)
                vpcex->EnqueueCmd(reinterpret_cast<PCommand>(&key));
        }
        ResetToolTip();
        break;
    default:
        break;
    }
}
void ApplicationBase::_HostMouseDown(const EVT &event)
{
    auto root = GraphicsObject::PgobScreen();
    PT point;
    auto gob = root ? root->PgobFromPt(static_cast<long>(event.x), static_cast<long>(event.y), &point) : nullptr;
    if (!gob)
    {
        _pgobMouse = nullptr;
        return;
    }
    const auto ticks = static_cast<std::uint32_t>(event.timestamp);
    if (_pgobMouse == gob && _cactMouse > 0 &&
        ticks - static_cast<std::uint32_t>(_tsMouse) < KauaiSDL::Main().DoubleClickTime())
        ++_cactMouse;
    else
        _cactMouse = 1;
    _tsMouse = ticks;
    if (_pgobMouse != gob && _pgobMouse && vpcex)
        vpcex->EnqueueCid(cidRollOff, _pgobMouse);
    _pgobMouse = gob;
    _xpMouse = klwMax;
    gob->MouseDown(point.xp, point.yp, _cactMouse, GrfcustCur());
}
bool ApplicationBase::_FTranslateKeyEvt(PEVT event, PCMD_KEY key)
{
    ClearPb(key, size(*key));
    key->cid = cidKey;
    key->cact = 1;
    key->grfcust = GrfcustCur() & ~kgrfcustKeys;
    if (event->modifiers & KauaiSDL::shift)
        key->grfcust |= fcustShift;
    if (event->modifiers & KauaiSDL::control)
        key->grfcust |= fcustCmd;
    if (event->modifiers & KauaiSDL::alt)
        key->grfcust |= fcustOption;
    if (event->kind == KauaiInput::Kind::character)
    {
        key->ch = event->character;
        return true;
    }
    key->vk = event->key;
    std::uint32_t character;
    if (_hostEvents.TakeCharacter(KauaiSDL::Main(), character))
        key->ch = character;
    else if (event->key == 8 || event->key == 9 || event->key == 13 || event->key == 27)
        key->ch = event->key;
    return true;
}
bool ApplicationBase::FGetNextKeyFromOsQueue(PCMD_KEY key)
{
    EVT event;
    return _hostEvents.NextKey(KauaiSDL::Main(), event) && !_FDispatchWindowShortcut(event) &&
           !_FDispatchShortcut(event) && _FTranslateKeyEvt(&event, key);
}
void ApplicationBase::TrackMouse(PGraphicsObject gob, PT *point)
{
    EVT event;
    while (_hostEvents.Next(KauaiSDL::Main(), event))
    {
        using K = KauaiInput::Kind;
        if (event.kind == K::motion || event.kind == K::buttonDown || event.kind == K::buttonUp)
            break;
        if (event.kind == K::keyDown)
            _FDispatchWindowShortcut(event);
        if (event.kind != K::keyDown && event.kind != K::keyUp && event.kind != K::character)
            _DispatchEvt(&event);
    }
    float x, y;
    bool down;
    KauaiSDL::Main().Mouse(x, y, down);
    point->xp = static_cast<long>(x);
    point->yp = static_cast<long>(y);
    gob->MapPt(point, cooHwnd, cooLocal);
}
void ApplicationBase::FlushUserEvents(ulong flags)
{
    _hostEvents.Flush(KauaiSDL::Main(), (flags & fevtMouse) != 0, (flags & fevtKey) != 0);
}

bool ApplicationBase::_FInitSDLWindow(const char *title, int width, int height)
{
    // Resource titles use the original Windows code page; SDL uses UTF-8.
    const auto count = KauaiText::ToUtf16(title, std::strlen(title), KauaiText::Encoding::WindowsAnsi, nullptr, 0);
    std::vector<std::uint16_t> words(count);
    if (count && !KauaiText::ToUtf16(title, std::strlen(title), KauaiText::Encoding::WindowsAnsi, words.data(), count))
        return fFalse;
    std::string utf8;
    if (!KauaiText::EncodeUtf8(words, utf8))
        return fFalse;
    if (!KauaiSDL::Main().Create(utf8.c_str(), width, height))
        return fFalse;
    if (_FBindSDLNativeWindow())
        return fTrue;
    _CloseSDLWindow();
    return fFalse;
}
void ApplicationBase::_CloseSDLWindow()
{
    _ReleaseSDLNativeWindow();
    KauaiSDL::Main().Close();
}
#ifndef WIN
PMenuBar vpmubCur = nullptr;

void AppendCrashLog(const char *category, const char *body)
{
    char directory[1024];
    if (!KauaiFiles::TempPath(directory, sizeof(directory))) return;
    std::string path = std::string(directory) + "/3dmmforever-crash.txt";
    KauaiFiles::OpenOptions options;
    options.write = true;
    auto file = KauaiFiles::Open(path.c_str(), options);
    if (!file)
    {
        options.creation = KauaiFiles::Creation::CreateExclusive;
        file = KauaiFiles::Open(path.c_str(), options);
    }
    if (!file) return;
    std::uint32_t bytes;
    std::string entry = std::string("\n[") + category + "] " + (body ? body : "") + "\n";
    if (KauaiFiles::Size(file, &bytes) && KauaiFiles::Seek(file, bytes))
        KauaiFiles::Write(file, entry.data(), static_cast<std::uint32_t>(entry.length()));
    KauaiFiles::Close(file);
}

bool ApplicationBase::_FInitOS()
{
    String title;
    GetStnAppName(&title);
    std::string utf8;
    return TextToUtf8(title.Psz(), utf8) && KauaiSDL::Main().Create(utf8.c_str(), 640, 480);
}

void ApplicationBase::Abort()
{
    _CloseSDLWindow();
    std::abort();
}

LegacyUlong DtsCaret()
{
    // SDL has no system caret timer; use the conventional half-second period.
    return 500;
}

#ifdef DEBUG
bool ApplicationBase::_FInitDebug()
{
    return fTrue;
}

bool ApplicationBase::FAssertProcApp(PSZS file, long line, PSZS message, void *data, long count)
{
    std::fprintf(stderr, "%s:%ld: %s\n", file ? file : "", line, message ? message : "Assertion failure");
    if (std::getenv("KAUAI_TEST_FAIL_FAST"))
    {
#ifndef _WIN32
        int depth = 0;
        _Unwind_Backtrace([](_Unwind_Context *context, void *opaque) {
            auto &frame = *static_cast<int *>(opaque);
            std::fprintf(stderr, "  frame %d: %p\n", frame++, reinterpret_cast<void *>(_Unwind_GetIP(context)));
            return frame < 32 ? _URC_NO_REASON : _URC_END_OF_STACK;
        }, &depth);
#endif
        std::fflush(stderr);
        std::_Exit(2);
    }
    // Early startup failures must remain diagnosable without a dialog host.
    if (!KauaiSDL::Main().Active())
        return fTrue;
    if (_fInAssert)
        return fFalse;
    _fInAssert = fTrue;
    String text, details, stack, filename;
    text.SetSzs(message ? message : const_cast<PSZS>("Assertion failure"));
    filename.SetSzs(file ? file : const_cast<PSZS>(""));
    details.FFormatSz(PszLit("%s:%d"), &filename, static_cast<LegacyLong>(line));
    if (data && count > 0)
    {
        const auto bytes = static_cast<const unsigned char *>(data);
        for (long offset = 0; offset < count && offset < 80; offset += 4)
        {
            std::uint32_t word = 0;
            const auto remaining = count - offset;
            std::memcpy(&word, bytes + offset, remaining < 4 ? remaining : 4);
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%08x ", word);
            stack.FAppendSz(hex);
        }
    }
    const auto choice = _TmcSDLAssert(&text, &details, &stack);
    _fInAssert = fFalse;
    if (choice == 2)
        Abort();
    // Dialog failure also breaks, rather than silently ignoring the assertion.
    return choice != 0;
}
#endif

bool ApplicationBase::_FBindSDLNativeWindow()
{
    KauaiSDL::WindowStatus status;
    if (!KauaiSDL::Main().WindowState(status)) return false;
    // SDL can acquire focus during creation, before this application's event
    // dispatcher is running. Establish that initial state as well as handling
    // subsequent SDL focus events.
    _Activate(status.keyboardFocus);
    return true;
}
void ApplicationBase::_ReleaseSDLNativeWindow()
{
    // POSIX uses the SDL window and CPU ports directly, with no native lease.
}
#endif
