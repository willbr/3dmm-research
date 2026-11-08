/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_SETTINGS_H
#define STUDIO_SETTINGS_H
#include "settings_store.h"
#include <string>
namespace StudioSettings
{
const KauaiSettings::Backend &IniBackend();
// UTF-8, absolute path. Debug test runs use a private temporary directory.
const std::string &IniPath();
} // namespace StudioSettings
#endif
