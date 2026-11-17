/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/settings.h"
#include <string.h>
typedef char SettingsUnitWidth[(sizeof(unsigned short) == 2) ? 1 : -1];

void mm_settings_request_init(MMSettingsRequest *request)
{
    memset(request, 0, sizeof(*request));
    request->name_unit = 1;
}
static void publish(int *output, int flag, int value, void *context, MMSettingsPublish observer)
{
    if (!output) return;
    *output = value;
    if (observer) observer(context, flag, value);
}
int mm_settings_access_observed(const MMSettingsBackend *backend, const void *key, const void *name,
    void *data, MMU32 capacity, const MMSettingsRequest *request, int *no_value, int *wrong_type,
    void *context, MMSettingsPublish observer)
{
    int created, write, kind, result, ok, terminated;
    void *handle;
    MMU32 bytes, offset;
    unsigned short ch;
    publish(wrong_type, MM_SETTINGS_WRONG_TYPE, 0, context, observer);
    if (!backend || !request || !key || !name || (!data && capacity) ||
        !backend->open || !backend->read || !backend->write || !backend->close ||
        (request->name_unit != 1 && request->name_unit != 2)) return 0;
    created = 0;
    handle = backend->open(backend->context, key, request->machine, &created);
    if (!handle) return 0;
    ok = 0;
    write = request->write || (created && request->set_default);
    if (!write) {
        bytes = capacity; kind = MM_SETTINGS_UNKNOWN;
        result = backend->read(backend->context, handle, name, data, &bytes, &kind);
        if (result == MM_SETTINGS_FOUND) {
            publish(wrong_type, MM_SETTINGS_WRONG_TYPE, kind != request->type, context, observer);
            publish(no_value, MM_SETTINGS_NO_VALUE, 0, context, observer);
            ok = 1; goto done;
        }
        if (result != MM_SETTINGS_MISSING || !request->set_default) {
            publish(no_value, MM_SETTINGS_NO_VALUE, result == MM_SETTINGS_MISSING, context, observer);
            ok = no_value && result == MM_SETTINGS_MISSING;
            goto done;
        }
    }
    bytes = capacity;
    if (request->type == MM_SETTINGS_NUMBER && capacity != sizeof(MMU32)) goto done;
    if (request->type == MM_SETTINGS_STRING && !request->write) {
        terminated = 0;
        for (offset = 0; capacity - offset >= request->name_unit; offset += request->name_unit) {
            if (request->name_unit == 1) ch = ((const unsigned char *)data)[offset];
            else memcpy(&ch, (const unsigned char *)data + offset, sizeof(ch));
            if (!ch) { bytes = offset + request->name_unit; terminated = 1; break; }
        }
        if (!terminated) goto done;
    }
    if (!backend->write(backend->context, handle, name, data, bytes, request->type)) goto done;
    publish(no_value, MM_SETTINGS_NO_VALUE, 0, context, observer);
    ok = 1;
done:
    backend->close(backend->context, handle);
    return ok;
}
int mm_settings_access(const MMSettingsBackend *backend, const void *key, const void *name,
    void *data, MMU32 capacity, const MMSettingsRequest *request, int *no_value, int *wrong_type)
{
    return mm_settings_access_observed(backend, key, name, data, capacity, request,
        no_value, wrong_type, NULL, NULL);
}
