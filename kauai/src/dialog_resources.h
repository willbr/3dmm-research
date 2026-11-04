/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_DIALOG_RESOURCES_H
#define KAUAI_DIALOG_RESOURCES_H
#include "dialog_resource.h"
namespace KauaiDialogs
{
using ResourceProvider = bool (*)(std::uint32_t, Resource &);
// The application supplies original compiled template bytes. Neither the
// provider nor the SDL dialog frontend needs an executable resource loader.
void SetResourceProvider(ResourceProvider provider);
bool ReadApplicationResource(std::uint32_t id, Resource &resource);
} // namespace KauaiDialogs
#endif
