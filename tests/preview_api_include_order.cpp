/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <mm/preview.h>
#include <algorithm>
#include <functional>
#include <unordered_map>
#if defined(size) || defined(NewObj) || defined(WIN) || defined(STRICT)
#error The preview header leaked legacy definitions
#endif
static_assert(sizeof(MM::PreviewInfo::firstFrame) == 4, "Frame numbers must retain wire width");
