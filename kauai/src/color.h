/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#ifndef KAUAI_COLOR_H
#define KAUAI_COLOR_H
#include <stdint.h>
// NOTE: this matches the Windows RGBQUAD structure
struct Color
{
    uint8_t bBlue;
    uint8_t bGreen;
    uint8_t bRed;
    uint8_t bZero;
};

#endif
