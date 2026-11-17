/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
/* Windows account and local-app-data services. */
#include "mm/native_users.h"
#include <string.h>
#define NOMINMAX
#include <windows.h>
#include <winnetwk.h>
#include <shlobj.h>
#ifdef UNICODE
typedef wchar_t NativeChar;
#else
typedef char NativeChar;
#endif
typedef char MMUserCharWidth[(sizeof(NativeChar) == sizeof(MMNativePathChar)) ? 1 : -1];

void mm_native_user_name(MMNativeUserName *result)
{
    NativeChar text[256] = {0}, provider[256] = {0};
    DWORD count, status, error;
    memset(result, 0, sizeof(*result));
    result->status = MM_NATIVE_USER_UNAVAILABLE;
    count = 256;
    status = WNetGetUser(NULL, text, &count);
    if (status == ERROR_EXTENDED_ERROR) {
        if (WNetGetLastError(&error, text, 256, provider, 256) == NO_ERROR) {
            result->status = MM_NATIVE_USER_PROVIDER_ERROR;
            memcpy(result->provider, provider, sizeof(provider));
            memcpy(result->message, text, sizeof(text));
            result->error = error;
        } else result->status = MM_NATIVE_USER_PROVIDER_ERROR_UNAVAILABLE;
    } else if (status == NO_ERROR && text[0]) {
        memcpy(result->name, text, sizeof(text));
        result->status = MM_NATIVE_USER_FOUND;
    }
}

size_t mm_native_user_data_root(MMNativePathChar *output, size_t capacity)
{
    NativeChar path[MAX_PATH];
    const char *suffix = "\\Microsoft Kids\\Users";
    size_t count, extra, i;
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path) != S_OK) return 0;
    count = 0;
    while (path[count]) ++count;
    extra = strlen(suffix);
    if (capacity <= count + extra) return 0;
    memcpy(output, path, count * sizeof(*output));
    for (i = 0; i <= extra; ++i) output[count + i] = (MMNativePathChar)suffix[i];
    return count + extra;
}
