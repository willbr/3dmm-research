/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SETTINGS_STORE_POSIX_H
#define KAUAI_SETTINGS_STORE_POSIX_H
#include "settings_store.h"
#include <string>
namespace KauaiSettings
{
enum class PosixHost
{
    Linux,
    MacOS
};
// Pure path selection; does not create directories or inspect the environment.
// Relative/empty XDG paths are ignored. User storage requires an absolute HOME
// unless Linux has an absolute XDG_CONFIG_HOME. Empty output means unavailable.
std::string ConfigurationRoot(PosixHost host, bool machine, const char *home, const char *xdgConfigHome);
// Native byte names, ASCII case-insensitive, at most 126 bytes per key/value.
// Each Access holds an exclusive namespace lock through read/default/write.
// User roots come from the environment; machine roots are /etc/3dmmforever or
// /Library/Application Support/3DMMForever and retain filesystem permissions.
const Backend &PosixBackend();
} // namespace KauaiSettings
#endif
