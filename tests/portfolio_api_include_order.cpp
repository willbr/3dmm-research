/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "portfolio_host.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Portfolio requests must not include the Windows SDK
#endif
static_assert(sizeof(LegacyLong) == 4, "request IDs retain their legacy width");
static_assert(sizeof(StudioPortfolio::Request::previews) == 4, "preview flags retain their legacy width");
