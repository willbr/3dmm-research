/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_SHORTCUTS_H
#define STUDIO_SHORTCUTS_H
#include "host_event.h"
#include "framedef.h"
#include "utestres.h"
namespace StudioInput
{
// Host modifier bits: Shift=1, Control=2, Alt=4. These bindings preserve
// acidMain/acidGlobal; the Windows resource parity test checks every key/modifier.
inline std::int32_t ShortcutCommand(std::uint32_t key, std::uint32_t modifiers, bool globalOnly)
{
    struct Binding
    {
        std::uint32_t key, modifiers;
        std::int32_t command;
        bool global;
    };
    static const Binding bindings[] = {{'C', 2, cidCopy, false},
                                       {'C', 3, cidShiftCopy, false},
                                       {'I', 3, cidInfo, true},
                                       {'M', 2, cidMap, false},
                                       {'N', 2, cidNew, false},
                                       {'O', 2, cidOpen, false},
                                       {'Q', 2, cidQuit, true},
                                       {'S', 2, cidSave, false},
                                       {'V', 2, cidPaste, false},
                                       {0x70, 0, cidHelpBook, false},
                                       {0x78, 0, cidToggleXY, false},
                                       {0x79, 2, cidWriteBmps, false},
                                       {0x79, 3, cidWriteBmpHires, false},
                                       {'X', 2, cidCut, false},
                                       {'X', 3, cidShiftCut, false},
                                       {'Y', 2, cidRedo, false},
                                       {'Z', 2, cidUndo, false}};
    for (const auto &binding : bindings)
        if (binding.key == key && binding.modifiers == modifiers && (!globalOnly || binding.global))
            return binding.command;
    return 0;
}
} // namespace StudioInput
#endif
