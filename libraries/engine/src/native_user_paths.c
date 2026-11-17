/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
/* Per-user POSIX storage-path policy shared by native hosts. */
#include "mm/native_users.h"
#include <string.h>

size_t mm_native_posix_user_data_root(int host, const char *home,
    const char *xdg, char *output, size_t capacity)
{
    int use_xdg;
    const char *base, *suffix;
    size_t count, extra, slash, length;
    if (output && capacity) output[0] = 0;
    use_xdg = host == MM_NATIVE_USER_LINUX && xdg && xdg[0] == '/';
    base = use_xdg ? xdg : home;
    if (!base || base[0] != '/') return 0;
    count = strlen(base);
    while (count > 1 && base[count - 1] == '/') --count;
    slash = base[count - 1] != '/';
    suffix = host == MM_NATIVE_USER_MACOS ?
        "Library/Application Support/3DMMForever/Users" :
        (use_xdg ? "3dmmforever/Users" : ".local/share/3dmmforever/Users");
    extra = strlen(suffix);
    if (count > (size_t)-1 - extra - slash - 1) return 0;
    length = count + slash + extra;
    if (!output) return length;
    if (capacity <= length) return 0;
    memcpy(output, base, count);
    if (slash) output[count++] = '/';
    memcpy(output + count, suffix, extra + 1);
    return length;
}
