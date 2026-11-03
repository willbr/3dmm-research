/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "platform_users.h"
std::string KauaiUsers::PosixDataRoot(PosixHost host, const char *home, const char *xdgDataHome)
{
    const bool xdg = host == PosixHost::Linux && xdgDataHome && xdgDataHome[0] == '/';
    const auto base = xdg ? xdgDataHome : home;
    if (!base || base[0] != '/')
        return {};
    std::string path(base);
    while (path.length() > 1 && path.back() == '/')
        path.pop_back();
    if (path.back() != '/')
        path += '/';
    if (host == PosixHost::MacOS)
        return path + "Library/Application Support/3DMMForever/Users";
    return path + (xdg ? "3dmmforever/Users" : ".local/share/3dmmforever/Users");
}
