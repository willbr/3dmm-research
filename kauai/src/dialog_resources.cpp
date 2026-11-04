/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dialog_resources.h"
namespace KauaiDialogs
{
namespace
{
ResourceProvider applicationResources = nullptr;
}
void SetResourceProvider(ResourceProvider provider)
{
    applicationResources = provider;
}
bool ReadApplicationResource(std::uint32_t id, Resource &resource)
{
    return applicationResources && applicationResources(id, resource);
}
} // namespace KauaiDialogs
