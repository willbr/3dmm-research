/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
/* Effective-account lookup and native POSIX user-data root. */
#define _POSIX_C_SOURCE 200809L
#include "mm/native_users.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#ifdef UNICODE
#error POSIX user paths require byte strings
#endif

void mm_native_user_name(MMNativeUserName *result)
{
    char *buffer, *grown;
    struct passwd entry, *found;
    size_t capacity;
    int error;
    memset(result, 0, sizeof(*result));
    result->status = MM_NATIVE_USER_UNAVAILABLE;
    capacity = 1024;
    buffer = (char *)malloc(capacity);
    if (!buffer) return;
    found = NULL;
    while ((error = getpwuid_r(geteuid(), &entry, buffer, capacity, &found)) == ERANGE) {
        if (capacity >= 1024 * 1024) { free(buffer); return; }
        capacity *= 2;
        grown = (char *)realloc(buffer, capacity);
        if (!grown) { free(buffer); return; }
        buffer = grown;
    }
    if (!error && found && found->pw_name && found->pw_name[0] && strlen(found->pw_name) <= 255) {
        strcpy(result->name, found->pw_name);
        result->status = MM_NATIVE_USER_FOUND;
    }
    free(buffer);
}

size_t mm_native_user_data_root(MMNativePathChar *output, size_t capacity)
{
    int host;
    if (!output || !capacity) return 0;
#ifdef __APPLE__
    host = MM_NATIVE_USER_MACOS;
#else
    host = MM_NATIVE_USER_LINUX;
#endif
    return mm_native_posix_user_data_root(host, getenv("HOME"), getenv("XDG_DATA_HOME"), output, capacity);
}
