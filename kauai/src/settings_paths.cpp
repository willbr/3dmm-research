/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "settings_store_posix.h"

std::string KauaiSettings::ConfigurationRoot(PosixHost host, bool machine, const char *home, const char *xdgConfigHome)
{
    if (machine)
        return host == PosixHost::MacOS ? "/Library/Application Support/3DMMForever" : "/etc/3dmmforever";
    const bool xdg = host == PosixHost::Linux && xdgConfigHome && xdgConfigHome[0] == '/';
    const auto base = xdg ? xdgConfigHome : home;
    if (!base || base[0] != '/')
        return {};
    std::string root(base);
    while (root.length() > 1 && root.back() == '/')
        root.pop_back();
    if (root.back() != '/')
        root += '/';
    if (host == PosixHost::MacOS)
        return root + "Library/Application Support/3DMMForever";
    return root + (xdg ? "3dmmforever" : ".config/3dmmforever");
}
