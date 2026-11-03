/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_CURSOR_FORMAT_H
#define KAUAI_CURSOR_FORMAT_H
#include <cstdint>
enum
{
    curtMonochrome = 0
};
// GGCR fixed record. Variable bytes contain MSB-first AND then XOR planes.
struct CursorOnFile
{
    std::int32_t curt;
    std::uint8_t xp, yp, dxp, dyp;
};
static_assert(sizeof(CursorOnFile) == 8, "cursor wire record must remain eight bytes");
const std::uint32_t kbomCurf = 0xC0000000;
#endif
