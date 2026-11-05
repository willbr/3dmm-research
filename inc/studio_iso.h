/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_ISO_H
#define STUDIO_ISO_H
#include "studio_content.h"
namespace StudioContent
{
// Native command-line path; an empty request permits adjacent-ISO discovery.
void SetISORequest(const Text &path);
bool HasISORequest();
Root PrepareISO(bool automaticDiscovery);
struct ISOStatus
{
    bool used = false, reused = false;
    std::string image, root, error; // UTF-8 diagnostics for the SDL host.
};
const ISOStatus &CurrentISO();
} // namespace StudioContent
#endif
