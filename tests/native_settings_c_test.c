#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "mm/native_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
static void name(MMNativePathChar *out, const char *value)
{ while (*value) *out++ = (MMNativePathChar)*value++; *out = 0; }
#ifdef _WIN32
static int visits, mask;
static int visit(void *owner, const MMNativePathChar *key, size_t count, int type, const unsigned char *data, size_t bytes)
{
    (void)owner;
    assert(count && key[count] == 0);
    ++visits;
    if (key[0] == 'V') { assert(type == MM_SETTINGS_NUMBER && bytes == 4 && data[0] == 42); mask |= 1; }
    else if (key[0] == 'T') { assert(type == MM_SETTINGS_STRING && bytes == 2 * sizeof(*key)); mask |= 2; }
    else if (key[0] == 'B') { assert(type == MM_SETTINGS_BINARY && bytes == 5 && data[1] == 255); mask |= 4; }
    else if (key[0] == 'E') { assert(type == MM_SETTINGS_BINARY && !bytes); mask |= 8; }
    else assert(0);
    return 1;
}
static int stop(void *owner, const MMNativePathChar *key, size_t count, int type, const unsigned char *data, size_t bytes)
{ (void)owner; (void)key; (void)count; (void)type; (void)data; (void)bytes; ++visits; return 0; }
#endif
int main(void)
{
    MMNativeSettingsContext *context;
    MMSettingsBackend b;
    MMSettingsRequest request;
    MMNativePathChar key[256], value_name[32], text[4];
    MMU32 value, bytes;
    unsigned char blob[5] = {0, 255, 42, 0, 17}, copy[5];
    void *handle;
    int created, kind, missing;
#ifdef _WIN32
    char path[256];
    HKEY fixture;
    DWORD disposition;
    int allocation_error;
    sprintf(path, "Software\\3DMMForever\\Tests\\C89Settings-%lu-%lu", (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    assert(RegCreateKeyExA(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, NULL, &fixture, &disposition) == ERROR_SUCCESS);
    assert(disposition == REG_CREATED_NEW_KEY);
    name(key, path);
#else
    char pattern[] = "/tmp/3mm-c89-settings-XXXXXX", root[512], directory[512], file[1024];
    DIR *dir;
    struct dirent *entry;
    assert(mkdtemp(pattern));
    assert(setenv("XDG_CONFIG_HOME", pattern, 1) == 0 && setenv("HOME", pattern, 1) == 0);
#ifdef __APPLE__
    assert(mm_settings_configuration_root(MM_SETTINGS_MACOS, 0, pattern, NULL, root, sizeof(root)));
#else
    assert(mm_settings_configuration_root(MM_SETTINGS_LINUX, 0, pattern, pattern, root, sizeof(root)));
#endif
    assert(strlen(root) + 16 < sizeof(directory));
    strcpy(directory, root); strcat(directory, "/k-74657374");
    name(key, "Test");
#endif
    context = mm_native_settings_create(0); assert(context);
    mm_native_settings_backend(context, &b);
    mm_settings_request_init(&request); request.name_unit = sizeof(MMNativePathChar); request.set_default = 1;
    name(value_name, "Value"); value = 42;
    assert(mm_settings_access(&b, key, value_name, &value, 4, &request, NULL, NULL));
    value = 99;
    assert(mm_settings_access(&b, key, value_name, &value, 4, &request, NULL, NULL) && value == 42);
    handle = b.open(b.context, key, 0, &created); assert(handle && !created);
    bytes = 1; kind = 77; copy[0] = 0xa5;
    assert(b.read(b.context, handle, value_name, copy, &bytes, &kind) == MM_SETTINGS_ERROR);
    assert(bytes == 4 && kind == 77 && copy[0] == 0xa5);
    b.close(b.context, handle);
    name(value_name, "Missing"); request.set_default = 0; missing = 0;
    assert(mm_settings_access(&b, key, value_name, &value, 4, &request, &missing, NULL) && missing);
    name(value_name, "Text"); text[0] = 'a'; text[1] = 0; text[2] = 'z'; text[3] = 0;
    request.type = MM_SETTINGS_STRING; request.set_default = 1;
    assert(mm_settings_access(&b, key, value_name, text, sizeof(text), &request, NULL, NULL));
    handle = b.open(b.context, key, 0, &created); assert(handle);
    bytes = 0; kind = 77;
    assert(b.read(b.context, handle, value_name, NULL, &bytes, &kind) == MM_SETTINGS_FOUND);
    assert(bytes == 2 * sizeof(*text) && kind == MM_SETTINGS_STRING);
    b.close(b.context, handle);
    name(value_name, "Blob"); request.type = MM_SETTINGS_BINARY; request.write = 1;
    assert(mm_settings_access(&b, key, value_name, blob, sizeof(blob), &request, NULL, NULL));
    memset(copy, 0, sizeof(copy)); request.write = 0;
    assert(mm_settings_access(&b, key, value_name, copy, sizeof(copy), &request, NULL, NULL) && !memcmp(copy, blob, sizeof(blob)));
    name(value_name, "Empty"); request.write = 1;
    assert(mm_settings_access(&b, key, value_name, NULL, 0, &request, NULL, NULL));
    request.write = 0;
    assert(mm_settings_access(&b, key, value_name, NULL, 0, &request, NULL, NULL));
#ifdef _WIN32
    assert(RegSetValueExA(fixture, "Ignored", 0, REG_EXPAND_SZ, (const BYTE *)"ignored", 8) == ERROR_SUCCESS);
    assert(RegSetValueExA(fixture, "", 0, REG_SZ, (const BYTE *)"marker", 7) == ERROR_SUCCESS);
    assert(mm_native_settings_values(context, key, 0, NULL, visit, &allocation_error));
    assert(!allocation_error && visits == 4 && mask == 15);
    visits = 0;
    assert(!mm_native_settings_values(context, key, 0, NULL, stop, &allocation_error) && visits == 1 && !allocation_error);
    RegCloseKey(fixture);
    assert(RegDeleteKeyExA(HKEY_CURRENT_USER, path, KEY_WOW64_32KEY, 0) == ERROR_SUCCESS);
    assert(mm_native_settings_values(context, key, 0, NULL, visit, &allocation_error));
#else
    dir = opendir(directory); assert(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        assert(strncmp(entry->d_name, ".tmp-", 5));
        sprintf(file, "%s/%s", directory, entry->d_name);
        assert(unlink(file) == 0);
    }
    closedir(dir);
    assert(rmdir(directory) == 0 && rmdir(root) == 0);
#ifdef __APPLE__
    sprintf(file, "%s/Library/Application Support", pattern); assert(rmdir(file) == 0);
    sprintf(file, "%s/Library", pattern); assert(rmdir(file) == 0);
#endif
    assert(rmdir(pattern) == 0);
#endif
    mm_native_settings_destroy(context);
    puts("C-only settings: real native persistence, payload/query/error semantics, context and cleanup passed.");
    return 0;
}
