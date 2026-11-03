/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include "studio_shortcuts.h"
#include <cstdio>
#include <type_traits>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
bool SendLetterKey(char letter, unsigned modifiers);
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "SDL application events failed: %d\n", __LINE__);                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
class EventApp : public ApplicationBase
{
  public:
    bool global = false;
    long shortcut = 0;
    bool Read(EVT &event)
    {
        return _hostEvents.Next(KauaiSDL::Main(), event, true);
    }
    bool Translate(EVT &event, CMD_KEY &key)
    {
        return _FTranslateKeyEvt(&event, &key);
    }
    bool PollKey(CMD_KEY &key)
    {
        shortcut = 0;
        // SDL can end a poll cycle before returning newly queued events.
        for (int i = 0; i < 20; ++i)
        {
            if (FGetNextKeyFromOsQueue(&key))
                return true;
            if (shortcut)
                return false;
        }
        return false;
    }
    void Dispatch(EVT &event)
    {
        _DispatchEvt(&event);
    }
    bool _FDispatchShortcut(const EVT &event) override
    {
        shortcut = event.kind == KauaiInput::Kind::keyDown
                       ? StudioInput::ShortcutCommand(event.key, event.modifiers, global)
                       : 0;
        return shortcut != 0;
    }
};
int main()
{
    static_assert(std::is_same<EVT, KauaiInput::Event>::value, "SDL must not dispatch Windows messages");
    auto &host = KauaiSDL::Main();
    CHECK(host.Create("SDL application events", 64, 48, true));
    auto app = NewObj EventApp;
    CHECK(app);
    EVT event;
    while (app->Read(event))
    {
    }
    CHECK(SendLetterKey('A', KauaiSDL::shift) && SendLetterKey('B', KauaiSDL::control));
    CHECK(app->Read(event) && event.kind == KauaiInput::Kind::keyDown && event.key == 'A');
    CMD_KEY key;
    CHECK(app->Translate(event, key));
    CHECK(key.vk == 'A' && (key.grfcust & kgrfcustKeys) == fcustShift);
    CHECK(app->Read(event) && event.key == 'B');
    CHECK(app->Translate(event, key) && (key.grfcust & kgrfcustKeys) == fcustCmd);
    CHECK(host.PushText("Caf\xC3\xA9"));
    const unsigned expectedBytes[] = {'C', 'a', 'f', 0xE9};
    for (unsigned byte : expectedBytes)
        CHECK(app->Read(event) && event.kind == KauaiInput::Kind::character && event.character == byte);
    CHECK(host.PushText("invalid\xC0\xAF"));
    CHECK(!app->Read(event));
    CHECK(host.PushText("ok"));
    CHECK(app->Read(event) && event.character == 'o');
    app->FlushUserEvents(fevtKey);
    CHECK(!app->Read(event));
    CHECK(host.PushPointer(12, 14, true));
    CHECK(!app->PollKey(key));
    CHECK(app->Read(event) && event.kind == KauaiInput::Kind::buttonDown && event.x == 12 && event.y == 14);
    CHECK(host.PushKey(8, true));
    CHECK(app->PollKey(key) && key.vk == 8 && key.ch == 8);
    CHECK(SendLetterKey('S', KauaiSDL::control));
    CHECK(!app->PollKey(key) && app->shortcut == cidSave);
    app->global = true;
    CHECK(SendLetterKey('S', KauaiSDL::control));
    CHECK(app->PollKey(key) && key.vk == 'S' && !app->shortcut);
    CHECK(SendLetterKey('Q', KauaiSDL::control));
    CHECK(!app->PollKey(key) && app->shortcut == cidQuit);
    CHECK(host.PushText("stale"));
    CHECK(app->Read(event) && event.character == 's');
    ReleasePpo(&app);
    app = NewObj EventApp;
    CHECK(app && !app->Read(event)); // Pending characters belong to their app.
    ReleasePpo(&app);
    host.Close();
    std::puts("SDL application events: plain values, text conversion/rejection, lookahead ordering/modifiers, "
              "shortcuts, flushing and queue lifetime passed.");
}
