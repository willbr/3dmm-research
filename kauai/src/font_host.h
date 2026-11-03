/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_FONT_HOST_H
#define KAUAI_FONT_HOST_H
#include "font_services.h"
#include <memory>
namespace KauaiFonts
{
// Host policy and accounting contain no native font handles or device contexts.
std::unique_ptr<Face> CreateStudioFace(Text family, std::int32_t pixels, bool bold, bool italic, bool underline);
bool StudioFontFamilies(std::vector<FamilyInfo> &families);
struct ProviderStats
{
    std::uint64_t portable = 0, native = 0, fallback = 0, nativeDraws = 0, nativeMeasurements = 0;
    std::uint64_t nativeBootstraps = 0;
    std::uint64_t portableEnumerations = 0, nativeEnumerations = 0;
};
ProviderStats FontProviderStats();
const char *StudioFontProvider();
void RecordFontFallback(bool measurement);
void RecordFontEnumeration(bool portable);
// Private adapters report their own native work; the portable service never
// invokes them. Studio retains its explicit compatibility path.
void RecordNativeFontBootstrap();
void RecordFontFace(bool portable);
void RecordFontCreationFallback();
} // namespace KauaiFonts
#endif
