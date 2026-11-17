/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "mm/native_settings.h"
#include <stdlib.h>
#include <string.h>
#define NOMINMAX
#include <windows.h>

struct MMNativeSettingsContext { int allow_test_namespace; };
typedef char SettingsCharWidth[(sizeof(TCHAR) == sizeof(MMNativePathChar)) ? 1 : -1];
MMNativeSettingsContext *mm_native_settings_create(int allow_test_namespace)
{
    MMNativeSettingsContext *context;
    context = (MMNativeSettingsContext *)malloc(sizeof(*context));
    if (context) context->allow_test_namespace = allow_test_namespace;
    return context;
}
void mm_native_settings_destroy(MMNativeSettingsContext *context) { free(context); }

static int root_key(MMNativeSettingsContext *context, int machine, int create,
    HKEY *root, HKEY *isolated, int *missing)
{
    const char *run, *suffix;
    char path[128];
    size_t length;
    LONG result;
    *root = machine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    *isolated = NULL; *missing = 0;
    if (!context) return 0;
    run = context->allow_test_namespace ? getenv("KAUAI_TEST_SETTINGS_RUN") : NULL;
    if (!run) return 1;
    length = strlen(run);
    if (!length || length > 64 || strspn(run, "0123456789abcdef-") != length) return 0;
    strcpy(path, "Software\\3DMMForever\\RetailOracle\\SDL-");
    strcat(path, run);
    suffix = machine ? "\\Machine" : "\\User";
    strcat(path, suffix);
    if (create) result = RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, 0,
        KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, isolated, NULL);
    else result = RegOpenKeyExA(HKEY_CURRENT_USER, path, 0, KEY_READ | KEY_WOW64_32KEY, isolated);
    if (!create && result == ERROR_FILE_NOT_FOUND) { *missing = 1; return 1; }
    if (result != ERROR_SUCCESS) return 0;
    *root = *isolated;
    return 1;
}
static void *settings_open(void *opaque, const void *key, int machine, int *created)
{
    HKEY root, isolated, handle;
    DWORD disposition;
    LONG result;
    int missing;
    if (!root_key((MMNativeSettingsContext *)opaque, machine, 1, &root, &isolated, &missing)) return NULL;
    result = RegCreateKeyEx(root, (const TCHAR *)key, 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, &handle, &disposition);
    if (isolated) RegCloseKey(isolated);
    if (result != ERROR_SUCCESS) return NULL;
    *created = disposition == REG_CREATED_NEW_KEY;
    return handle;
}
static int stored_type(DWORD type)
{
    return type == REG_DWORD ? MM_SETTINGS_NUMBER : type == REG_SZ ? MM_SETTINGS_STRING :
        type == REG_BINARY ? MM_SETTINGS_BINARY : MM_SETTINGS_UNKNOWN;
}
static int settings_read(void *context, void *handle, const void *name, void *data, MMU32 *bytes, int *type)
{
    DWORD native_bytes, native_type;
    LONG result;
    (void)context;
    native_bytes = *bytes; native_type = 0;
    result = RegQueryValueEx((HKEY)handle, (const TCHAR *)name, NULL, &native_type, (BYTE *)data, &native_bytes);
    *bytes = native_bytes;
    if (result != ERROR_SUCCESS) return result == ERROR_FILE_NOT_FOUND ? MM_SETTINGS_MISSING : MM_SETTINGS_ERROR;
    *type = stored_type(native_type);
    return MM_SETTINGS_FOUND;
}
static int settings_write(void *context, void *handle, const void *name, const void *data, MMU32 bytes, int type)
{
    DWORD native_type;
    (void)context;
    native_type = type == MM_SETTINGS_NUMBER ? REG_DWORD : type == MM_SETTINGS_STRING ? REG_SZ :
        type == MM_SETTINGS_BINARY ? REG_BINARY : REG_NONE;
    return RegSetValueEx((HKEY)handle, (const TCHAR *)name, 0, native_type, (const BYTE *)data, bytes) == ERROR_SUCCESS;
}
static void settings_close(void *context, void *handle)
{ (void)context; RegCloseKey((HKEY)handle); }
void mm_native_settings_backend(MMNativeSettingsContext *context, MMSettingsBackend *backend)
{
    backend->context = context; backend->open = settings_open; backend->read = settings_read;
    backend->write = settings_write; backend->close = settings_close;
}
int mm_native_settings_values(MMNativeSettingsContext *context, const MMNativePathChar *key,
    int machine, void *owner, MMNativeSettingsVisit visit, int *allocation_error)
{
    HKEY root, isolated, handle;
    LONG status;
    DWORD max_name, max_data, index, count, length, type;
    TCHAR *name;
    unsigned char *bytes;
    size_t name_count;
    int missing, ok, kind;
    if (allocation_error) *allocation_error = 0;
    if (!root_key(context, machine, 0, &root, &isolated, &missing)) return 0;
    if (missing) return 1;
    status = RegOpenKeyEx(root, (const TCHAR *)key, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &handle);
    if (isolated) RegCloseKey(isolated);
    if (status == ERROR_FILE_NOT_FOUND) return 1;
    if (status != ERROR_SUCCESS) return 0;
    name = NULL; bytes = NULL; ok = 0; max_name = 0; max_data = 0;
    if (RegQueryInfoKey(handle, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &max_name, &max_data, NULL, NULL) != ERROR_SUCCESS ||
        max_data > 16 * 1024 * 1024) goto done;
    name_count = (size_t)max_name + 1;
    if (!name_count || name_count > (size_t)-1 / sizeof(*name)) goto done;
    name = (TCHAR *)malloc(name_count * sizeof(*name));
    if (max_data) bytes = (unsigned char *)malloc(max_data);
    if (!name || (max_data && !bytes)) {
        if (allocation_error) *allocation_error = 1;
        goto done;
    }
    for (index = 0;; ++index) {
        count = (DWORD)name_count; length = max_data;
        status = RegEnumValue(handle, index, name, &count, NULL, &type, bytes, &length);
        if (status == ERROR_NO_MORE_ITEMS) { ok = 1; break; }
        if (status != ERROR_SUCCESS || count >= name_count || length > max_data) break;
        kind = stored_type(type);
        if (kind != MM_SETTINGS_UNKNOWN && count && !visit(owner, (const MMNativePathChar *)name, count, kind, bytes, length)) break;
    }
done:
    free(bytes); free(name); RegCloseKey(handle);
    return ok;
}
