#ifndef MM_NATIVE_SETTINGS_H
#define MM_NATIVE_SETTINGS_H
#include "settings.h"
#include "native_files.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMNativeSettingsContext MMNativeSettingsContext;
/* Explicit context owns the POSIX temporary-number sequence and its mutex.
 * Destroy only after all handles close and all calls finish. Native names use
 * native_files.h's profile. POSIX names are ASCII-folded and hex encoded, with
 * a 126-byte limit. Its namespace lock covers open through close, including
 * default transactions. Stored KPS1 records/payloads are unchanged. Windows
 * uses the existing HKCU/HKLM keys and 32-bit view. allow_test_namespace enables
 * the existing validated KAUAI_TEST_SETTINGS_RUN isolation, never implicitly.
 * Initialization failure returns NULL; backend init requires a live context. */
MMNativeSettingsContext *mm_native_settings_create(int allow_test_namespace);
void mm_native_settings_destroy(MMNativeSettingsContext *);
void mm_native_settings_backend(MMNativeSettingsContext *, MMSettingsBackend *);
#ifdef _WIN32
/* Read-only snapshot. Missing key is empty success. Known, named values are
 * visited in native order. Names/data borrow reusable buffers until return.
 * Returning zero stops/fails, preserving prior visits. No callback may throw.
 * allocation_error is optional, cleared first, set on C scratch allocation
 * failure so the legacy vector adapter can retain its exception contract. */
typedef int (*MMNativeSettingsVisit)(void *, const MMNativePathChar *name, size_t count,
    int type, const unsigned char *data, size_t bytes);
int mm_native_settings_values(MMNativeSettingsContext *, const MMNativePathChar *key,
    int machine, void *context, MMNativeSettingsVisit, int *allocation_error);
#endif
#ifdef __cplusplus
}
#endif
#endif
