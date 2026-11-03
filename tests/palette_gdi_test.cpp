/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "palette_state.h"
#include <cstdio>
#include <cstring>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
int main()
{
#ifndef KAUAI_SDL3
    vwig.hdcApp = GetDC(nullptr);
    if (!vwig.hdcApp || (GetDeviceCaps(vwig.hdcApp, RASTERCAPS) & RC_PALETTE))
        return 1; // This oracle compares the original truecolor-display policy.
#endif
    auto requested = DynamicArray::PglNew(sizeof(Color), 300);
    if (!requested)
        return 1;
    KauaiPixels::PaletteState portable;
    Color input[300];
    int cases = 0;
    for (unsigned flags = 0; flags < 8; ++flags)
    {
        // Seed every entry before testing retained/reserved slots.
        if (!requested->FSetIvMac(256))
            return 1;
        for (int i = 0; i < 300; ++i)
        {
            input[i] = {static_cast<byte>(i * 17), static_cast<byte>(i * 37), static_cast<byte>(i * 73), 123};
            if (i < 256)
                requested->Put(i, &input[i]);
        }
        portable.Set(input, 256, KauaiPixels::paletteIdentity);
        GraphicsPort::SetActiveColors(requested, fpalIdentity);
        for (int iteration = 0; iteration <= 300; ++iteration)
        {
            const int count = (iteration * 97) % 301;
            if (!requested->FSetIvMac(count))
                return 1;
            for (int i = 0; i < count; ++i)
            {
                input[i] = {static_cast<byte>(i * 17 + iteration), static_cast<byte>(i * 37 + flags),
                            static_cast<byte>(i * 73 + iteration), 231};
                requested->Put(i, &input[i]);
            }
            portable.Set(input, count, flags);
            GraphicsPort::SetActiveColors((!count && (flags & 1)) ? nullptr : requested, flags);
            auto observed = GraphicsPort::PglclrGetPalette();
            if (!observed || observed->IvMac() != static_cast<long>(portable.Count()) ||
                (portable.Count() &&
                 std::memcmp(observed->QvGet(0), portable.Colors(), portable.Count() * sizeof(Color))))
            {
                std::fprintf(stderr, "Palette mismatch: flags=%u input=%d case=%d\n", flags, count, cases);
                return 1;
            }
            ReleasePpo(&observed);
            ++cases;
        }
    }
    ReleasePpo(&requested);
#ifndef KAUAI_SDL3
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#else
    if (vwig.hdcApp)
        return 1;
#endif
    std::printf(
        "Palette adapter: %d sequential palette updates matched, covering all flags and input lengths 0..300.\n",
        cases);
}
