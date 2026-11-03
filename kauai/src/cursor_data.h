/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_CURSOR_DATA_H
#define KAUAI_CURSOR_DATA_H
#include "cursor_format.h"
#include <vector>
class DataBlock;
namespace KauaiCursors
{
struct Monochrome
{
    int width = 0, height = 0, pitch = 0, hotX = 0, hotY = 0;
    std::vector<std::uint8_t> andBits, xorBits;
};
// Select the closest supported image no larger than the preferred size.
// Output is unchanged on failure. No cursor resources or OS APIs are used.
bool Read(DataBlock &block, int preferredWidth, int preferredHeight, Monochrome &image);
} // namespace KauaiCursors
#endif
