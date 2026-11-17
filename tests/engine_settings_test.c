#include "mm/settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct Store {
    int opened, created, read_result, write_ok, kind;
    unsigned opens, reads, writes, closes;
    MMU32 bytes;
    unsigned char data[16];
    int *closing_flag;
} Store;
static void init(Store *s)
{
    memset(s, 0, sizeof(*s)); s->opened = 1; s->write_ok = 1;
    s->read_result = MM_SETTINGS_MISSING; s->kind = MM_SETTINGS_UNKNOWN;
}
static void *open_store(void *opaque, const void *key, int machine, int *created)
{
    Store *s;
    s = (Store *)opaque; assert(key && machine == 1); ++s->opens;
    *created = s->created; return s->opened ? s : NULL;
}
static int read_store(void *opaque, void *handle, const void *name, void *data, MMU32 *bytes, int *kind)
{
    Store *s;
    s = (Store *)opaque; assert(handle == s && name); ++s->reads;
    if (s->read_result != MM_SETTINGS_FOUND) return s->read_result;
    if (*bytes < s->bytes) return MM_SETTINGS_ERROR;
    if (s->bytes) memcpy(data, s->data, s->bytes);
    *bytes = s->bytes; *kind = s->kind; return MM_SETTINGS_FOUND;
}
static int write_store(void *opaque, void *handle, const void *name, const void *data, MMU32 bytes, int kind)
{
    Store *s;
    s = (Store *)opaque; assert(handle == s && name && bytes <= sizeof(s->data)); ++s->writes;
    if (!s->write_ok) return 0;
    s->bytes = bytes; s->kind = kind;
    if (bytes) memcpy(s->data, data, bytes);
    return 1;
}
static void close_store(void *opaque, void *handle)
{
    Store *s;
    s = (Store *)opaque; assert(handle == s); ++s->closes;
    if (s->closing_flag) { assert(*s->closing_flag == 0); *s->closing_flag = 9; }
}
typedef struct Publications { int *flag; unsigned count; } Publications;
static void published(void *opaque, int flag, int value)
{
    Publications *p;
    p = (Publications *)opaque;
    assert(*p->flag == value);
    assert(p->count < 3);
    assert(flag == (p->count < 2 ? MM_SETTINGS_WRONG_TYPE : MM_SETTINGS_NO_VALUE));
    assert(value == (p->count == 1 ? 1 : 0));
    ++p->count;
}
static void policy(unsigned unit)
{
    MMSettingsBackend b;
    MMSettingsRequest r;
    Store s;
    MMU32 number;
    int missing, wrong;
    unsigned char text[9] = {0, 'a', 0, 0, 0, 'z', 0, 0, 0};
    unsigned short wide[4] = {'a', 0, 'z', 0};
    void *value;
    Publications publications;
    int aliased;
    init(&s);
    b.context = &s; b.open = open_store; b.read = read_store; b.write = write_store; b.close = close_store;
    mm_settings_request_init(&r); assert(r.type == MM_SETTINGS_NUMBER && !r.write && r.name_unit == 1);
    r.machine = 1; r.name_unit = unit; number = 7;
    assert(!mm_settings_access(&b, "k", "v", &number, 4, &r, NULL, NULL));
    assert(s.reads == 1 && !s.writes && s.closes == 1 && number == 7);
    missing = 0;
    assert(mm_settings_access(&b, "k", "v", &number, 4, &r, &missing, NULL) && missing == 1);
    r.set_default = 1;
    assert(mm_settings_access(&b, "k", "v", &number, 4, &r, &missing, NULL) && !missing);
    assert(s.writes == 1 && s.kind == MM_SETTINGS_NUMBER && s.bytes == 4);
    s.read_result = MM_SETTINGS_ERROR; missing = 9;
    assert(!mm_settings_access(&b, "k", "v", &number, 4, &r, &missing, NULL) && !missing && s.writes == 1);
    init(&s); s.created = 1;
    assert(mm_settings_access(&b, "k", "v", &number, 4, &r, NULL, NULL));
    assert(!s.reads && s.writes == 1 && s.closes == 1);
    init(&s); s.opened = 0; missing = 9; wrong = 9;
    assert(!mm_settings_access(&b, "k", "v", &number, 4, &r, &missing, &wrong));
    assert(missing == 9 && wrong == 0 && !s.reads && !s.closes);
    init(&s); s.write_ok = 0; missing = 9;
    assert(!mm_settings_access(&b, "k", "v", &number, 4, &r, &missing, NULL));
    assert(missing == 9 && s.writes == 1 && s.closes == 1);
    init(&s); r.type = MM_SETTINGS_STRING;
    if (unit == 2) memcpy(text + 1, wide, sizeof(wide));
    value = text + 1;
    assert(mm_settings_access(&b, "k", "v", value, 4 * unit, &r, NULL, NULL));
    assert(s.bytes == 2 * unit && !memcmp(s.data, value, s.bytes));
    r.write = 1;
    assert(mm_settings_access(&b, "k", "v", value, 4 * unit, &r, NULL, NULL) && s.bytes == 4 * unit);
    r.write = 0; init(&s); memset(text, 'x', sizeof(text));
    assert(!mm_settings_access(&b, "k", "v", value, 3, &r, NULL, NULL));
    assert(!s.writes && s.closes == 1);
    init(&s); r.type = MM_SETTINGS_BINARY; r.write = 1;
    assert(mm_settings_access(&b, "k", "v", NULL, 0, &r, NULL, NULL) && !s.bytes);
    s.read_result = MM_SETTINGS_FOUND; s.kind = MM_SETTINGS_STRING; r.write = 0; wrong = 0;
    assert(mm_settings_access(&b, "k", "v", NULL, 0, &r, NULL, &wrong) && wrong);
    r.write = 1; r.type = MM_SETTINGS_NUMBER;
    assert(!mm_settings_access(&b, "k", "v", value, 1, &r, NULL, NULL));
    init(&s); wrong = 9; missing = 9;
    assert(!mm_settings_access(&b, NULL, "v", value, 4, &r, &missing, &wrong));
    assert(!s.opens && missing == 9 && wrong == 0);
    b.read = NULL;
    assert(!mm_settings_access(&b, "k", "v", value, 4, &r, NULL, NULL) && !s.opens);
    b.read = read_store; init(&s);
    s.read_result = MM_SETTINGS_FOUND; s.kind = MM_SETTINGS_STRING;
    aliased = 7; publications.flag = &aliased; publications.count = 0; s.closing_flag = &aliased;
    r.write = 0;
    assert(mm_settings_access_observed(&b, "k", "v", value, 4, &r, &aliased, &aliased, &publications, published));
    assert(publications.count == 3 && aliased == 9 && s.closes == 1);
}
static void path(int host, int machine, const char *home, const char *xdg, const char *expected)
{
    char output[128];
    size_t count;
    count = strlen(expected);
    assert(mm_settings_configuration_root(host, machine, home, xdg, NULL, 0) == count);
    memset(output, 'x', sizeof(output));
    assert(mm_settings_configuration_root(host, machine, home, xdg, output, count + 1) == count);
    assert(!strcmp(output, expected) && output[count + 1] == 'x');
    if (count) { assert(!mm_settings_configuration_root(host, machine, home, xdg, output, count)); assert(!output[0]); }
    output[0] = 'x';
    assert(!mm_settings_configuration_root(host, machine, home, xdg, output, 0) && output[0] == 'x');
}
int main(void)
{
    policy(1); policy(2);
    path(MM_SETTINGS_LINUX, 0, "/home/user/", "relative", "/home/user/.config/3dmmforever");
    path(MM_SETTINGS_LINUX, 0, NULL, "/config///", "/config/3dmmforever");
    path(MM_SETTINGS_LINUX, 0, "/", NULL, "/.config/3dmmforever");
    path(MM_SETTINGS_LINUX, 0, NULL, "/", "/3dmmforever");
    path(MM_SETTINGS_LINUX, 0, "relative", NULL, "");
    path(MM_SETTINGS_MACOS, 0, "/Users/user///", "/ignored", "/Users/user/Library/Application Support/3DMMForever");
    path(MM_SETTINGS_MACOS, 0, NULL, "/ignored", "");
    path(MM_SETTINGS_LINUX, 1, NULL, NULL, "/etc/3dmmforever");
    path(MM_SETTINGS_MACOS, 1, NULL, NULL, "/Library/Application Support/3DMMForever");
    puts("C89 settings: one/two-byte policy, default/error/publication/cleanup and root selection passed.");
    return 0;
}
