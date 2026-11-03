/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_shortcuts.h"
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#ifdef _WINDOWS_
#error Public host events and shortcut bindings must not import the Windows SDK
#endif
#ifdef _WIN32
#include <windows.h>
#endif
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Studio shortcut check failed: %d\n", __LINE__);                                      \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv)
{
    static_assert(std::is_trivially_copyable<KauaiInput::Event>::value, "Host events must be plain values");
    CHECK(StudioInput::ShortcutCommand('C', 2, false) == cidCopy);
    CHECK(StudioInput::ShortcutCommand('C', 3, false) == cidShiftCopy);
    CHECK(!StudioInput::ShortcutCommand('C', 2, true));
    CHECK(StudioInput::ShortcutCommand('Q', 2, true) == cidQuit);
    CHECK(StudioInput::ShortcutCommand('I', 3, true) == cidInfo);
    CHECK(StudioInput::ShortcutCommand(0x70, 0, false) == cidHelpBook);
    CHECK(!StudioInput::ShortcutCommand(0x70, 1, false));
    CHECK(!StudioInput::ShortcutCommand('S', 6, false));
#ifdef _WIN32
    if (argc == 2)
    {
        auto module = LoadLibraryExA(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        CHECK(module);
        for (int scope = 0; scope < 2; ++scope)
        {
            const bool global = scope != 0;
            auto resource = LoadAcceleratorsA(module, MAKEINTRESOURCEA(global ? acidGlobal : acidMain));
            CHECK(resource);
            ACCEL entries[32];
            const int count = CopyAcceleratorTableA(resource, entries, 32);
            CHECK(count > 0 && count < 32);
            for (unsigned key = 0; key < 256; ++key)
                for (unsigned modifiers = 0; modifiers < 8; ++modifiers)
                {
                    int expected = 0;
                    for (int i = 0; i < count; ++i)
                    {
                        const auto &entry = entries[i];
                        CHECK(entry.fVirt & FVIRTKEY);
                        const unsigned mods = ((entry.fVirt & FSHIFT) ? 1 : 0) | ((entry.fVirt & FCONTROL) ? 2 : 0) |
                                              ((entry.fVirt & FALT) ? 4 : 0);
                        if (entry.key == key && mods == modifiers)
                        {
                            expected = entry.cmd;
                            break;
                        }
                    }
                    CHECK(StudioInput::ShortcutCommand(key, modifiers, global) == expected);
                }
        }
        FreeLibrary(module);
    }
#endif
    std::puts("Studio shortcuts: SDK-free event values, main/global bindings and optional 4096 Windows resource "
              "comparisons passed.");
}
