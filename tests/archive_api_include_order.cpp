/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <algorithm>
#include <string>
#include <vector>
#include <three_mm/archive.h>
#if defined(size) || defined(NewObj) || defined(WIN) || defined(STRICT)
#error The public archive API leaked legacy macros or compile definitions.
#endif
#ifdef __has_include
#if __has_include("kauai_core.h") || __has_include("soc_core.h")
#error The public archive target exposed legacy include directories.
#endif
#endif
static_assert(sizeof(ThreeMM::Id::type) == 4, "archive identifiers must retain their wire width");
