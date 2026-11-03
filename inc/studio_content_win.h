/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_CONTENT_WIN_H
#define STUDIO_CONTENT_WIN_H
#include "studio_content.h"
namespace StudioContent
{
// Registry adapter entry point, also permits isolated HKCU fixtures in tests.
Products ReadRegistryProducts(const Char *key, bool machine);
} // namespace StudioContent
#endif
