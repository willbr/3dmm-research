/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "gfx.h"
#include "gob.h"
#undef size
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The high-level graphics API must not import the Windows SDK
#endif
static_assert(sizeof(FontDescription) == 20, "font description must use 32-bit words");
static_assert(sizeof(AbstractPattern) == 8, "pattern bytes must retain their existing layout");
static_assert(sizeof(AbstractColor) == 4, "abstract colors must retain their 32-bit layout");
static_assert(sizeof(decltype(GraphicsDrawingData::grfgdd)) == 4 && sizeof(decltype(GraphicsDrawingData::dxpPen)) == 4,
              "drawing flags and pen sizes must remain 32-bit words");
static_assert(std::is_same<decltype(GraphicsDrawingData::prcsClip), RC *>::value, "clipping must use core geometry");
static void Check(bool value)
{
    if (!value)
        std::exit(1);
}
int main()
{
    AbstractPattern a = {}, b = {};
    Check(a.FSolidBack() && !a.FSolidFore() && a == b && !(a != b));
    a.SetSolidFore();
    Check(a.FSolidFore() && !a.FSolidBack() && a != b);
    a.Invert();
    Check(a == b);
    for (int bit = 0; bit < 64; ++bit)
    {
        a.SetSolidBack();
        a.rgb[bit / 8] = static_cast<byte>(1u << (bit % 8));
        Check(!a.FSolidBack() && !a.FSolidFore() && a != b);
        auto original = a;
        a.Invert();
        a.Invert();
        Check(a == original);
        a.SetSolidFore();
        a.rgb[bit / 8] ^= static_cast<byte>(1u << (bit % 8));
        Check(!a.FSolidBack() && !a.FSolidFore());
    }
    std::puts("Graphics API: no SDK imports, portable font/clip layout and all 64 pattern bits passed.");
}
