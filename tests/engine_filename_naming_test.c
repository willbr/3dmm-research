#include "mm/filename_naming.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{ if (!value) { fprintf(stderr, "Filename naming check failed at %d\n", line); exit(1); } }
#define CHECK(x) check(!!(x), __LINE__)
typedef union Buffer { unsigned char bytes[257]; unsigned short words[257]; } Buffer;
typedef struct Name { MMFilenameState state; Buffer buffer; MMFilenameValue value; } Name;
typedef struct Fixture {
    MMFilenameNamingHost host;
    MMFilenameNamingState state;
    Name value, cached;
    unsigned size, clock_calls, temp_calls, info_calls, copies, validations, assertions, name_copies, errors, failed;
    MMU32 ticks;
    int status[20], directory, hidden, temp_ok, last_flags, last_code;
    MMI32 error_type, error_length;
    void *error_owner;
    char queried[20][256];
} Fixture;
static unsigned get(const void *p, unsigned size, int i)
{ return size == 1 ? ((const unsigned char *)p)[i] : ((const unsigned short *)p)[i]; }
static void put(void *p, unsigned size, int i, unsigned ch)
{ if (size == 1) ((unsigned char *)p)[i] = (unsigned char)ch; else ((unsigned short *)p)[i] = (unsigned short)ch; }
static void set(Fixture *f, Name *n, const char *text, MMI32 type)
{
    Buffer b; int i, count = (int)strlen(text); CHECK(count <= 255);
    for (i = 0; i <= count; ++i) put(&b, f->size, i, (unsigned char)text[i]);
    mm_string_value_set(&n->value.path, &b, count); n->state.type = type;
}
static void equals(Name *n, const char *text)
{
    int i, count = (int)strlen(text); CHECK(get(&n->buffer, n->value.path.unit, 0) == (unsigned)count);
    for (i = 0; i <= count; ++i) CHECK(get(&n->buffer, n->value.path.unit, i + 1) == (unsigned char)text[i]);
}
static unsigned upper(void *c, unsigned ch) { (void)c; return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch; }
static int separator(void *c, unsigned ch) { (void)c; return ch == '/' || ch == '\\'; }
static void validate(void *c, const MMFilenameValue *v, int flags)
{ Fixture *f = (Fixture *)c; CHECK(v->state == &((Name *)v->owner)->state); ++f->validations; f->last_flags = flags; }
static void assertion(void *c, int code) { Fixture *f = (Fixture *)c; CHECK(code > 0); ++f->assertions; }
static void copy_check(void *c, const void *a, void *b, size_t count)
{ Fixture *f = (Fixture *)c; CHECK(a && b && count > 0); ++f->copies; }
static void copy_name(void *c, const MMFilenameValue *a, const MMFilenameValue *b)
{ Fixture *f = (Fixture *)c; ++f->name_copies; a->state->type = b->state->type; mm_string_value_copy(&a->path, b->path.buffer); }
static void issue(void *c, const MMFilenameValue *v, int code)
{
    Fixture *f = (Fixture *)c; ++f->errors; f->last_code = code; f->error_type = v->state->type;
    f->error_length = (MMI32)get(v->path.buffer, v->path.unit, 0); f->error_owner = v->owner;
}
static void failed(void *c) { Fixture *f = (Fixture *)c; ++f->failed; }
static MMU32 ticks(void *c) { Fixture *f = (Fixture *)c; ++f->clock_calls; return f->ticks; }
static MMI32 temp(void *c, void *out, MMI32 capacity)
{
    Fixture *f = (Fixture *)c; int i; ++f->temp_calls; CHECK(capacity == 255);
    if (!f->temp_ok) return 0;
    for (i = 0; i < 9; ++i) put(out, f->size, i, (unsigned char)"/cached/"[i]);
    return 1; /* The original copies the terminated path, not this result. */
}
static int info(void *c, const void *path, MMFilenameInfo *out)
{
    Fixture *f = (Fixture *)c; unsigned i = 0, index = f->info_calls++;
    CHECK(index < 20);
    while (get(path, f->size, (int)i)) { CHECK(i < 255); f->queried[index][i] = (char)get(path, f->size, (int)i); ++i; }
    f->queried[index][i] = 0; out->directory = f->directory; out->hidden = f->hidden; out->system = 0;
    return f->status[index];
}
static void init(Fixture *f, unsigned size)
{
    int i; Name *n; MMFilenameNamingHost *h;
    memset(f, 0, sizeof(*f)); f->size = size; f->temp_ok = 1; h = &f->host;
    h->path.context = h->path.value.context = f; h->path.value.upper = upper;
    h->path.value.separator = separator; h->path.value.validate = validate; h->path.value.assertion = assertion;
    h->path.value.failed = failed; h->path.issue = issue; h->path.info = info;
    h->path.copy_name = copy_name; h->clock = ticks; h->temp_path = temp;
    for (i = 0; i < 2; ++i) {
        n = i ? &f->cached : &f->value; n->value.state = &n->state; n->value.owner = n;
        n->value.path.buffer = &n->buffer; n->value.path.unit = size; n->value.path.context = f;
        n->value.path.check_copy = copy_check; mm_filename_value_clear(&n->value, &h->path.value);
    }
    for (i = 0; i < 20; ++i) f->status[i] = MM_FILENAME_MISSING;
    set(f, &f->value, "/p/", MM_FILENAME_DIRECTORY);
}
static int unique(Fixture *f, MMI32 type)
{ return mm_filename_unique(&f->value.value, &f->host, &f->state, type); }
static int temporary(Fixture *f, MMI32 type)
{ return mm_filename_temp(&f->value.value, &f->host, &f->state, &f->cached.value, type); }
int main(void)
{
    Fixture f; unsigned size, sequence, k, i; MMU32 expected, bits;
    char long_path[251]; const MMU32 clocks[] = {0, 0x7ffeUL, 0x80000000UL, 0xffffffffUL};
    for (size = 1; size <= 2; ++size) {
        for (sequence = 0; sequence <= 65535U; ++sequence) {
            init(&f, size); f.state.sequence = sequence; f.ticks = clocks[sequence & 3];
            CHECK(unique(&f, 0x545854L)); expected = ((MMU32)sequence + 1) & 65535UL;
            CHECK(f.state.sequence == expected && f.clock_calls == 1 && f.info_calls == 1);
            bits = (f.ticks + expected) & 65535UL;
            for (k = 0; k < 4; ++k)
                CHECK(get(&f.value.buffer, size, 8 + (int)k) == (unsigned)"0123456789ABCDEF"[(bits >> (12 - 4 * k)) & 15]);
            CHECK(f.value.state.type == 0x545854L && f.errors == 0);
        }
        init(&f, size); set(&f, &f.value, "/p/Temp0001", 0);
        CHECK(unique(&f, MM_FILENAME_NIL)); equals(&f.value, "/p/Temp0003"); CHECK(f.state.sequence == 2 && f.info_calls == 1);
        init(&f, size); set(&f, &f.value, "/p/Temp0001.TXT", 0x545854L);
        CHECK(unique(&f, 0x545854L)); equals(&f.value, "/p/Temp0001.TXT"); CHECK(f.state.sequence == 1);
        init(&f, size); f.status[0] = MM_FILENAME_FOUND; f.status[1] = MM_FILENAME_INFO_ERROR;
        CHECK(unique(&f, 0x545854L)); equals(&f.value, "/p/Temp0006.TXT");
        CHECK(f.state.sequence == 3 && f.clock_calls == 1 && f.info_calls == 3 && f.errors == 1);
        CHECK(f.error_type == 0x545854L && f.error_length == 15 && f.error_owner == &f.value);
        init(&f, size); f.status[0] = MM_FILENAME_FOUND; f.directory = 1;
        CHECK(unique(&f, 0)); CHECK(f.errors == 1 && f.last_code == MM_FILENAME_MISMATCH && f.info_calls == 2);
        init(&f, size); f.status[0] = MM_FILENAME_FOUND; f.hidden = 1;
        CHECK(unique(&f, 0)); CHECK(f.errors == 1 && f.last_code == MM_FILENAME_HIDDEN && f.info_calls == 2);
        init(&f, size); for (i = 0; i < 20; ++i) f.status[i] = MM_FILENAME_FOUND;
        CHECK(!unique(&f, 0x545854L)); equals(&f.value, "");
        CHECK(f.state.sequence == 21 && f.info_calls == 20 && f.clock_calls == 1 && f.errors == 1);
        CHECK(f.error_type == MM_FILENAME_NIL && f.error_length == 0 && f.last_flags == MM_FILENAME_EMPTY);
        init(&f, size); memset(long_path, 'a', 250); long_path[0] = long_path[249] = '/'; long_path[250] = 0;
        set(&f, &f.value, long_path, MM_FILENAME_DIRECTORY);
        CHECK(unique(&f, 0x545854L)); equals(&f.value, "Temp0003.TXT");
        CHECK(f.state.sequence == 2 && f.info_calls == 1 && f.failed == 1 && f.assertions > 0);
        init(&f, size); f.temp_ok = 0;
        CHECK(!temporary(&f, 0)); equals(&f.value, "/p/"); equals(&f.cached, "");
        CHECK(f.state.sequence == 0 && !f.clock_calls && f.temp_calls == 1 && f.errors == 1);
        CHECK(f.error_type == MM_FILENAME_DIRECTORY && f.error_length == 3 && f.cached.state.type == MM_FILENAME_NIL);
        f.temp_ok = 1; CHECK(temporary(&f, 0x545854L)); equals(&f.value, "/cached/Temp0001.TXT"); equals(&f.cached, "/cached/");
        CHECK(f.cached.state.type == MM_FILENAME_DIRECTORY && f.temp_calls == 2 && f.name_copies == 1);
        f.temp_ok = 0; f.info_calls = 0; CHECK(temporary(&f, 0x4142L)); equals(&f.value, "/cached/Temp0002.AB");
        CHECK(f.temp_calls == 2 && f.state.sequence == 2 && f.name_copies == 2);
        for (i = 0; i < 20; ++i) f.status[i] = MM_FILENAME_FOUND;
        f.info_calls = 0; CHECK(!temporary(&f, 0)); equals(&f.value, ""); equals(&f.cached, "/cached/");
        CHECK(f.cached.state.type == MM_FILENAME_DIRECTORY && f.state.sequence == 23);
        init(&f, size); f.host.path.copy_name = NULL; f.host.path.issue = NULL;
        f.host.path.value.validate = NULL; f.host.path.value.assertion = NULL; f.host.path.value.failed = NULL;
        CHECK(temporary(&f, 0)); equals(&f.value, "/cached/Temp0001"); CHECK(!f.name_copies);
        CHECK(f.copies > 0);
        /* Names are unreserved: a collision retry followed by a clock tick
         * may choose a previously returned name. Callers needing a distinct
         * in-memory set must check that set as well as disk existence. */
        init(&f, size); CHECK(unique(&f, 0x545854L)); equals(&f.value, "/p/Temp0001.TXT");
        set(&f, &f.value, "/p/Temp0001.TXT", 0x545854L);
        f.status[1] = MM_FILENAME_FOUND;
        CHECK(unique(&f, 0x545854L)); equals(&f.value, "/p/Temp0005.TXT");
        set(&f, &f.value, "/p/Temp0001.TXT", 0x545854L); f.ticks = 1;
        CHECK(unique(&f, 0x545854L)); equals(&f.value, "/p/Temp0005.TXT");
    }
    puts("C89 filename naming: all 16-bit sequences, retries, failure publication and temp cache passed");
    return 0;
}
