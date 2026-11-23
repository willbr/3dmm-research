/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef MOVIE_DATA_H
#define MOVIE_DATA_H
#include "kauai_core.h"
#include "tagman.h"
#include <cstdint>
#include "mm/movie_data.h"
#include "mm/movie_owner.h"
#include "mm/rollcall_storage.h"
#include "mm/movie_root.h"

struct MovieFilePrefix
{
    int16_t bo;       // byte order
    int16_t osk;      // which system wrote this
    DataVersion dver; // chunky file version
};
static_assert(sizeof(MovieFilePrefix) == 8, "MovieFilePrefix on-disk layout drift");
const ByteOrderMask kbomMfp = 0x55000000;

// Fixed-width GST actor row used by the compiler and format fixtures.
struct RollCallActorEntryOnFile
{
    int32_t arid;
    int32_t cactRef;
    uint32_t grfbrws;
    TAGOnFile tagTmpl;
};
static_assert(sizeof(RollCallActorEntryOnFile) == 28, "RollCallActorEntryOnFile wire format drift");

#endif
