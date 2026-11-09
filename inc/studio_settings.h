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
// Read a snapshot without creating settings, importing registry values or
// taking the mutable backend's lock. Missing files/keys are reported as Missing.
KauaiSettings::ReadResult ReadString(const std::string &section, const std::string &key, std::string &value);
} // namespace StudioSettings
#endif
