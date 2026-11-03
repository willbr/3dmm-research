/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_FONT_NAMES_H
#define KAUAI_FONT_NAMES_H
#include "font_services.h"
#include <string>
namespace KauaiFonts
{
// Font identifiers are UTF-8 inside the catalog; movie strings retain their
// legacy encoding. Reject malformed names, embedded NULs and lossy conversion.
bool NameToUtf8(Text name, std::string &utf8);
bool NameFromUtf8(const std::string &utf8, Encoding encoding, std::vector<std::uint8_t> &name);
} // namespace KauaiFonts
#endif
