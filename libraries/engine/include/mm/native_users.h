/* Native account and per-user storage services. */
#ifndef MM_NATIVE_USERS_H
#define MM_NATIVE_USERS_H
#include "native_files.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {
    MM_NATIVE_USER_FOUND, MM_NATIVE_USER_UNAVAILABLE,
    MM_NATIVE_USER_PROVIDER_ERROR, MM_NATIVE_USER_PROVIDER_ERROR_UNAVAILABLE
};
enum { MM_NATIVE_USER_LINUX, MM_NATIVE_USER_MACOS };
/* Caller-owned, initialized on every query. Native strings contain at most
 * 255 code units plus NUL. Windows preserves WNet provider errors; POSIX uses
 * the effective uid's account, never USER/LOGNAME. No borrowed storage. */
typedef struct MMNativeUserName {
    int status;
    MMNativePathChar name[256], provider[256], message[256];
    MMU32 error;
} MMNativeUserName;
void mm_native_user_name(MMNativeUserName *result);
/* Does not create directories. Capacity includes NUL. Returns length, or zero
 * on failure/insufficient capacity, clearing nonempty output. NULL/zero output
 * is accepted and returns zero. UNICODE profiles must agree with native_files. */
size_t mm_native_user_data_root(MMNativePathChar *output, size_t capacity);
/* Pure byte-path policy, available on every host. Absolute Linux XDG overrides
 * HOME; macOS ignores XDG. Trailing slashes are normalized. NULL output measures
 * required length (excluding NUL); an insufficient non-NULL buffer is cleared
 * and returns zero. Inputs/output must not overlap. */
size_t mm_native_posix_user_data_root(int host, const char *home,
    const char *xdg, char *output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
