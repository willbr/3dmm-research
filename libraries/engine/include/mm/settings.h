/* Shared typed preference access policy. */
#ifndef MM_SETTINGS_H
#define MM_SETTINGS_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_SETTINGS_NUMBER, MM_SETTINGS_STRING, MM_SETTINGS_BINARY, MM_SETTINGS_UNKNOWN };
enum { MM_SETTINGS_FOUND, MM_SETTINGS_MISSING, MM_SETTINGS_ERROR };
typedef struct MMSettingsBackend {
    void *context;
    void *(*open)(void *, const void *key, int machine, int *created);
    int (*read)(void *, void *handle, const void *name, void *data, MMU32 *bytes, int *type);
    int (*write)(void *, void *handle, const void *name, const void *data, MMU32 bytes, int type);
    void (*close)(void *, void *handle);
} MMSettingsBackend;
typedef struct MMSettingsRequest {
    int type, write, set_default, machine;
    unsigned name_unit;
} MMSettingsRequest;
void mm_settings_request_init(MMSettingsRequest *);
/* Names/data borrow caller storage. name_unit is 1 or 2, without encoding
 * conversion; capacity and backend lengths are bytes. All callbacks are
 * required and must not throw. Every opened handle is closed once. Defaults
 * replace only missing values/new stores. Read errors never write a default.
 * Found values succeed even with a different type (reported via wrong_type).
 * wrong_type is cleared before validation. Open/write/validation failures leave
 * no_value untouched; reads publish missing/error, success clears it. Missing
 * is successful only with no_value. String defaults stop at the first complete
 * native NUL; explicit string writes retain capacity. */
int mm_settings_access(const MMSettingsBackend *, const void *key, const void *name,
    void *data, MMU32 capacity, const MMSettingsRequest *, int *no_value, int *wrong_type);
enum { MM_SETTINGS_NO_VALUE, MM_SETTINGS_WRONG_TYPE };
typedef void (*MMSettingsPublish)(void *, int flag, int value);
/* Same policy, with an optional notification immediately after each output
 * assignment (including initial wrong-type clearing), before subsequent
 * callbacks/close. Only supplied outputs are notified. This lets legacy bool
 * adapters publish in C's order, preserving aliasing and callback observations.
 * Notifications must not throw; request storage remains live throughout. */
int mm_settings_access_observed(const MMSettingsBackend *, const void *key, const void *name,
    void *data, MMU32 capacity, const MMSettingsRequest *, int *no_value, int *wrong_type,
    void *context, MMSettingsPublish);
enum { MM_SETTINGS_LINUX, MM_SETTINGS_MACOS };
/* Absolute XDG/HOME and machine-root policy. No I/O. NULL output measures the
 * length excluding NUL; insufficient nonempty output is cleared. No overlap. */
size_t mm_settings_configuration_root(int host, int machine, const char *home,
    const char *xdg, char *output, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
