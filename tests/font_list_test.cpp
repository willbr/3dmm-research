/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "font_host.h"
#include "font_names.h"
#include <cstdio>
#include <cstdlib>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
int main()
{
#ifdef _WIN32
    _putenv_s("KAUAI_FONT_PROVIDER", "freetype");
#ifdef WIN
    if (vwig.hdcApp)
        return 1;
#endif
#else
    setenv("KAUAI_FONT_PROVIDER", "freetype", 1);
#endif
    if (!vntl.FInit())
        return 1;
    const auto stats = KauaiFonts::FontProviderStats();
    if (!stats.portableEnumerations || stats.nativeEnumerations || stats.nativeBootstraps)
        return 1;
    const auto count = vntl.OnnMac();
    if (count < 2 || vntl.OnnSystem() < 0 || vntl.OnnSystem() >= count)
        return 1;
    for (long i = 0; i < count; ++i)
    {
        String name;
        vntl.GetStn(i, &name);
        long found;
        if (!name.Cch() || !vntl.FGetOnn(&name, &found) || found != i || vntl.OnnMapStn(&name) != i)
            return 1;
        if (i)
        {
            String previous;
            vntl.GetStn(i - 1, &previous);
            if (previous.FcmpCompareUser(&name, fstnIgnoreCase) != fcmpLt)
                return 1;
        }
    }
    achar unknownName[] = {'N', 'o', 'n', 'e', 'x', 'i', 's', 't', 'e', 'n', 't', ' ', 'f', 'o', 'n', 't', 0};
    String unknown(unknownName);
    if (vntl.OnnMapStn(&unknown) != vntl.OnnSystem())
        return 1;
    for (auto family : {"Arial", "Courier New"})
    {
        String name;
        std::vector<std::uint8_t> encoded;
        if (!KauaiFonts::NameFromUtf8(
                family, sizeof(achar) == 2 ? KauaiFonts::Encoding::utf16 : KauaiFonts::Encoding::legacyBytes, encoded))
            return 1;
        name.SetRgch(reinterpret_cast<achar *>(encoded.data()), static_cast<long>(encoded.size() / sizeof(achar)));
        long index;
        if (vntl.FGetOnn(&name, &index) && vntl.FFixedPitch(index) != (family[0] == 'C'))
            return 1;
    }
    std::printf("Portable font list: %ld sorted, unique, name-mapped families without a device context.\n", count);
    return 0;
}
