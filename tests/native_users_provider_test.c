/* Deterministic native-service failures without changing process accounts. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "mm/native_users.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static int mode, calls;
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winnetwk.h>
#include <shlobj.h>
#ifdef UNICODE
typedef wchar_t TestChar;
#else
typedef char TestChar;
#endif
static void text(TestChar *out, const char *value)
{
    while (*value) *out++ = (TestChar)*value++;
    *out = 0;
}
static DWORD query(const TestChar *unused, TestChar *out, DWORD *size)
{
    assert(!unused && *size == 256);
    ++calls;
    if (mode == 1 || mode == 2) return ERROR_EXTENDED_ERROR;
    if (mode == 3) return ERROR_MORE_DATA;
    if (mode == 4) return ERROR_NO_NETWORK;
    if (mode != 5) text(out, "account");
    return NO_ERROR;
}
static DWORD extended(DWORD *error, TestChar *out, DWORD size, TestChar *provider, DWORD capacity)
{
    assert(size == 256 && capacity == 256);
    ++calls;
    if (mode == 2) return ERROR_NO_NETWORK;
    text(out, "message"); text(provider, "provider"); *error = 1234;
    return NO_ERROR;
}
static HRESULT root(HWND window, int folder, HANDLE token, DWORD flags, TestChar *out)
{
    assert(!window && folder == CSIDL_LOCAL_APPDATA && !token && !flags);
    if (mode) return E_FAIL;
    text(out, "C:\\Local");
    return S_OK;
}
#undef WNetGetUser
#undef WNetGetLastError
#undef SHGetFolderPath
#define WNetGetUser query
#define WNetGetLastError extended
#define SHGetFolderPath root
#include "../libraries/engine/src/native_users_win.c"
#else
#include <errno.h>
#include <pwd.h>
#include <unistd.h>
static size_t last_capacity;
static char account[257];
static void *allocate(size_t size)
{
    if (mode == 7) return NULL;
    return malloc(size);
}
static void *grow(void *pointer, size_t size)
{
    if (mode == 8) return NULL;
    return realloc(pointer, size);
}
static int query(uid_t uid, struct passwd *entry, char *buffer, size_t size, struct passwd **found)
{
    assert(uid == geteuid() && buffer);
    last_capacity = size;
    ++calls;
    if (mode == 1 || mode == 8 || (mode == 2 && calls < 3)) return ERANGE;
    if (mode == 3) return EIO;
    *found = mode == 4 ? NULL : entry;
    entry->pw_name = mode == 5 ? NULL : account;
    return 0;
}
#define getpwuid_r query
#define malloc allocate
#define realloc grow
#include "../libraries/engine/src/native_users_posix.c"
#undef malloc
#undef realloc
#endif
int main(void)
{
    MMNativeUserName name;
#ifdef _WIN32
    MMNativePathChar buffer[64];
    mode = 0; calls = 0; mm_native_user_name(&name);
    assert(name.status == MM_NATIVE_USER_FOUND && name.name[0] == 'a' && calls == 1);
    mode = 1; calls = 0; mm_native_user_name(&name);
    assert(name.status == MM_NATIVE_USER_PROVIDER_ERROR && calls == 2);
    assert(name.error == 1234 && name.provider[0] == 'p' && name.message[0] == 'm' && !name.name[0]);
    mode = 2; mm_native_user_name(&name);
    assert(name.status == MM_NATIVE_USER_PROVIDER_ERROR_UNAVAILABLE && !name.error && !name.provider[0]);
    for (mode = 3; mode <= 5; ++mode) {
        mm_native_user_name(&name);
        assert(name.status == MM_NATIVE_USER_UNAVAILABLE && !name.name[0] && !name.message[0]);
    }
    mode = 0;
    assert(mm_native_user_data_root(buffer, 64) == strlen("C:\\Local\\Microsoft Kids\\Users"));
    assert(buffer[0] == 'C');
    mode = 1;
    assert(!mm_native_user_data_root(buffer, 64) && !buffer[0]);
#else
    memset(account, 'a', 255); account[255] = 0;
    mode = 0; calls = 0; mm_native_user_name(&name);
    assert(name.status == MM_NATIVE_USER_FOUND && strlen(name.name) == 255 && calls == 1);
    account[255] = 'a'; account[256] = 0; mm_native_user_name(&name);
    assert(name.status == MM_NATIVE_USER_UNAVAILABLE);
    account[0] = 0; mm_native_user_name(&name);
    assert(name.status == MM_NATIVE_USER_UNAVAILABLE);
    strcpy(account, "account");
    mode = 1; calls = 0; mm_native_user_name(&name);
    assert(name.status == MM_NATIVE_USER_UNAVAILABLE && calls == 11 && last_capacity == 1024 * 1024);
    mode = 2; calls = 0; mm_native_user_name(&name);
    assert(name.status == MM_NATIVE_USER_FOUND && calls == 3 && last_capacity == 4096);
    for (mode = 3; mode <= 8; ++mode) {
        if (mode == 6) continue;
        calls = 0; mm_native_user_name(&name);
        assert(name.status == MM_NATIVE_USER_UNAVAILABLE && !name.name[0] && !name.error);
        if (mode == 7) assert(!calls);
        if (mode == 8) assert(calls == 1);
    }
#endif
    return 0;
}
