#include "mm/native_users.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void path(int host, const char *home, const char *xdg, const char *expected)
{
    char buffer[256];
    size_t n;
    n = strlen(expected);
    assert(mm_native_posix_user_data_root(host, home, xdg, NULL, 0) == n);
    memset(buffer, 'x', sizeof(buffer));
    assert(mm_native_posix_user_data_root(host, home, xdg, buffer, n + 1) == n);
    assert(!strcmp(buffer, expected));
    assert(buffer[n + 1] == 'x');
    if (n) {
        assert(!mm_native_posix_user_data_root(host, home, xdg, buffer, n));
        assert(!buffer[0]);
    }
    buffer[0] = 'x';
    assert(!mm_native_posix_user_data_root(host, home, xdg, buffer, 0));
    assert(buffer[0] == 'x');
}
int main(void)
{
    MMNativeUserName name;
    MMNativePathChar output[4096], guard;
    size_t length;
    path(MM_NATIVE_USER_LINUX, "/home/user/", "relative", "/home/user/.local/share/3dmmforever/Users");
    path(MM_NATIVE_USER_LINUX, NULL, "/data///", "/data/3dmmforever/Users");
    path(MM_NATIVE_USER_LINUX, "/", NULL, "/.local/share/3dmmforever/Users");
    path(MM_NATIVE_USER_LINUX, NULL, "/", "/3dmmforever/Users");
    path(MM_NATIVE_USER_LINUX, "relative", NULL, "");
    path(MM_NATIVE_USER_LINUX, NULL, "relative", "");
    path(MM_NATIVE_USER_MACOS, "/Users/user///", "/ignored", "/Users/user/Library/Application Support/3DMMForever/Users");
    path(MM_NATIVE_USER_MACOS, NULL, "/ignored", "");
    memset(&name, 0xff, sizeof(name));
    mm_native_user_name(&name);
    assert(name.status >= MM_NATIVE_USER_FOUND && name.status <= MM_NATIVE_USER_PROVIDER_ERROR_UNAVAILABLE);
    assert(name.name[255] == 0 && name.provider[255] == 0 && name.message[255] == 0);
    if (name.status == MM_NATIVE_USER_FOUND) assert(name.name[0]);
    guard = 'x';
    assert(!mm_native_user_data_root(NULL, 20));
    assert(!mm_native_user_data_root(&guard, 0) && guard == 'x');
    assert(!mm_native_user_data_root(&guard, 1) && guard == 0);
    length = mm_native_user_data_root(output, 4096);
    if (length) {
        assert(output[length] == 0);
        assert(!mm_native_user_data_root(output, length) && !output[0]);
        assert(mm_native_user_data_root(output, length + 1) == length);
    }
    puts("C-only native users: path policy, native query and capacity checks passed.");
    return 0;
}
