/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef LEGACY_TYPES_H
#define LEGACY_TYPES_H
#include <cstdint>
#include <cinttypes>
// Kauai's original 'long' means 32 bits in data, containers and arithmetic.
// Keep long's Windows C++ ABI while making that contract explicit on LP64.
#ifdef _WIN32
using LegacyLong = long;
using LegacyUlong = unsigned long;
#define LEGACY_PRId "ld"
#define LEGACY_PRIu "lu"
#define LEGACY_PRIx "lx"
#define LEGACY_PRIX "lX"
#else
using LegacyLong = std::int32_t;
using LegacyUlong = std::uint32_t;
#define LEGACY_PRId PRId32
#define LEGACY_PRIu PRIu32
#define LEGACY_PRIx PRIx32
#define LEGACY_PRIX PRIX32
#endif
static_assert(sizeof(LegacyLong) == 4 && sizeof(LegacyUlong) == 4, "legacy words must remain 32 bits");
#endif
