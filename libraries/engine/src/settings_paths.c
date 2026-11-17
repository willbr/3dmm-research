/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/settings.h"
#include <string.h>
size_t mm_settings_configuration_root(int host, int machine, const char *home,
    const char *xdg, char *output, size_t capacity)
{
    const char *base, *suffix;
    size_t count, extra, slash, length;
    int use_xdg;
    if (output && capacity) output[0] = 0;
    if (machine) {
        base = host == MM_SETTINGS_MACOS ? "/Library/Application Support/3DMMForever" : "/etc/3dmmforever";
        count = strlen(base); slash = 0; suffix = "";
    } else {
        use_xdg = host == MM_SETTINGS_LINUX && xdg && xdg[0] == '/';
        base = use_xdg ? xdg : home;
        if (!base || base[0] != '/') return 0;
        count = strlen(base);
        while (count > 1 && base[count - 1] == '/') --count;
        slash = base[count - 1] != '/';
        suffix = host == MM_SETTINGS_MACOS ? "Library/Application Support/3DMMForever" :
            (use_xdg ? "3dmmforever" : ".config/3dmmforever");
    }
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
