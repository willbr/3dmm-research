#include "mm/file_owner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{ if (!value) { fprintf(stderr, "File owner check failed at %d\n", line); exit(1); } }
#define CHECK(x) check((x), __LINE__)
enum { FAIL_SEEK = 1, FAIL_READ = 2, FAIL_WRITE = 4, FAIL_RESIZE = 8, FAIL_SIZE = 16, FAIL_FLUSH = 32, FAIL_DELETE = 64 };
enum { EV_ENTER = 10, EV_LEAVE, EV_OPEN, EV_CLOSE, EV_FLUSH, EV_DELETE, EV_SEEK, EV_SIZE, EV_READ, EV_WRITE, EV_RESIZE, EV_ISSUE, EV_VALIDATE, EV_ARGUMENT, EV_ASSERT };
typedef struct Event { int kind, detail, depth; MMFileOwner state; } Event;
typedef struct Fixture {
    MMFileOwner state;
    MMFileOwnerHost host;
    int depth, maximum, fail, count, opens, plan[8], plan_count;
    MMU32 flags[8], position, length;
    int creates[8];
    unsigned char data[32];
    Event events[256];
} Fixture;
static void event(Fixture *f, int kind, int detail)
{
    Event *e;
    CHECK(f->count < 256);
    e = &f->events[f->count++]; e->kind = kind; e->detail = detail;
    e->depth = f->depth; e->state = f->state;
}
static void enter(void *c)
{
    Fixture *f = (Fixture *)c;
    ++f->depth; if (f->depth > f->maximum) f->maximum = f->depth;
    event(f, EV_ENTER, 0);
}
static void leave(void *c)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0); event(f, EV_LEAVE, 0); --f->depth;
}
static void *open_file(void *c, int create, MMU32 flags)
{
    Fixture *f = (Fixture *)c;
    int index = f->opens++;
    CHECK(f->depth > 0 && index < 8);
    f->flags[index] = flags; f->creates[index] = create; event(f, EV_OPEN, 0);
    if (index < f->plan_count && !f->plan[index]) return NULL;
    return f->data;
}
static void close_file(void *c, void *handle)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0 && handle == f->data && f->state.handle == handle && f->state.open);
    event(f, EV_CLOSE, 0);
}
static int flush(void *c, void *handle)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0 && handle == f->data); event(f, EV_FLUSH, 0);
    return !(f->fail & FAIL_FLUSH);
}
static int remove_file(void *c)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0 && !f->state.open && !f->state.handle); event(f, EV_DELETE, 0);
    return !(f->fail & FAIL_DELETE);
}
static int seek(void *c, void *handle, MMU32 position)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0 && handle == f->data); event(f, EV_SEEK, (int)position);
    if (f->fail & FAIL_SEEK) return 0;
    f->position = position; return 1;
}
static int resize(void *c, void *handle, MMU32 bytes)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0 && handle == f->data); event(f, EV_RESIZE, (int)bytes);
    if (f->fail & FAIL_RESIZE) return 0;
    CHECK(bytes <= sizeof(f->data)); f->length = bytes; return 1;
}
static int size_file(void *c, void *handle, MMU32 *bytes)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0 && handle == f->data); event(f, EV_SIZE, 0);
    *bytes = f->length; return !(f->fail & FAIL_SIZE);
}
static int read_file(void *c, void *handle, void *p, MMU32 bytes)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0 && handle == f->data); event(f, EV_READ, (int)bytes);
    if ((f->fail & FAIL_READ) || f->position > f->length || bytes > f->length - f->position) return 0;
    memcpy(p, f->data + f->position, bytes); f->position += bytes; return 1;
}
static int write_file(void *c, void *handle, const void *p, MMU32 bytes)
{
    Fixture *f = (Fixture *)c;
    CHECK(f->depth > 0 && handle == f->data && f->state.wrote); event(f, EV_WRITE, (int)bytes);
    if (f->fail & FAIL_WRITE) return 0;
    CHECK(f->position <= sizeof(f->data) && bytes <= sizeof(f->data) - f->position);
    memcpy(f->data + f->position, p, bytes); f->position += bytes;
    if (f->position > f->length) f->length = f->position;
    return 1;
}
static void issue(void *c, int code) { event((Fixture *)c, EV_ISSUE, code); }
static void validate(void *c, int base) { event((Fixture *)c, EV_VALIDATE, base); }
static void arguments(void *c, int op, const void *p, MMI32 bytes, MMI32 position)
{ (void)p; (void)bytes; (void)position; event((Fixture *)c, EV_ARGUMENT, op); }
static void assertion(void *c, int code) { event((Fixture *)c, EV_ASSERT, code); }
static void init(Fixture *f, MMU32 flags)
{
    memset(f, 0, sizeof(*f)); mm_file_owner_init(&f->state, flags);
    f->host.context = f; f->host.enter = enter; f->host.leave = leave;
    f->host.open = open_file; f->host.close = close_file; f->host.flush = flush;
    f->host.remove = remove_file; f->host.seek = seek; f->host.resize = resize;
    f->host.size = size_file; f->host.read = read_file; f->host.write = write_file;
    f->host.issue = issue; f->host.validate = validate; f->host.arguments = arguments;
    f->host.assertion = assertion;
}
static int count(Fixture *f, int kind)
{
    int i, n = 0;
    for (i = 0; i < f->count; ++i) if (f->events[i].kind == kind) ++n;
    return n;
}
static Event *last(Fixture *f, int kind)
{
    int i;
    for (i = f->count; i-- > 0;) if (f->events[i].kind == kind) return &f->events[i];
    CHECK(0); return NULL;
}
static void opened(Fixture *f)
{
    CHECK(mm_file_owner_open(&f->state, &f->host, 1, MM_FILE_WRITE | MM_FILE_DENY_WRITE));
    CHECK(f->state.open && f->state.ever_open && f->state.handle == f->data && !f->depth);
    f->count = 0; f->maximum = 0;
}
int main(void)
{
    Fixture f;
    unsigned char bytes[8];
    int i, failures[] = {FAIL_SEEK, FAIL_READ, FAIL_WRITE, FAIL_RESIZE, FAIL_SIZE};
    init(&f, 0x80000000UL | MM_FILE_TEMP | MM_FILE_MARK); opened(&f);
    CHECK(f.flags[0] == (MM_FILE_WRITE | MM_FILE_DENY_WRITE | MM_FILE_TEMP));
    CHECK(f.state.flags == (0x80000000UL | MM_FILE_TEMP | MM_FILE_MARK | MM_FILE_WRITE | MM_FILE_DENY_WRITE));
    CHECK(mm_file_owner_open(&f.state, &f.host, 0, MM_FILE_WRITE) && f.opens == 1);
    CHECK(mm_file_owner_open(&f.state, &f.host, 1, MM_FILE_WRITE));
    CHECK(last(&f, EV_ASSERT)->detail == MM_FILE_CHECK_CREATE && !count(&f, EV_CLOSE));
    f.count = 0;
    CHECK(mm_file_owner_write(&f.state, &f.host, "abcdef", 6, 0));
    f.host.debug_read = 1;
    CHECK(mm_file_owner_read(&f.state, &f.host, bytes, 6, 0) && !memcmp(bytes, "abcdef", 6));
    CHECK(f.maximum == 2 && count(&f, EV_SIZE) == 1 && !f.depth);
    CHECK(mm_file_owner_resize(&f.state, &f.host, 4) && mm_file_owner_size(&f.state, &f.host) == 4);
    f.count = 0;
    CHECK(!mm_file_owner_read(&f.state, &f.host, bytes, 1, 4));
    CHECK(last(&f, EV_ASSERT)->detail == MM_FILE_CHECK_EOF);
    CHECK(last(&f, EV_ISSUE)->state.error == MM_FILE_ERROR_NONE && f.state.error == MM_FILE_ERROR_READ);
    /* Failed upgrades retain old flags/handle/error; double failure latches critical. */
    init(&f, MM_FILE_DENY_WRITE | MM_FILE_MARK); CHECK(mm_file_owner_open(&f.state, &f.host, 0, f.state.flags));
    f.plan_count = 3; f.plan[1] = 0; f.plan[2] = 1; f.state.error = MM_FILE_ERROR_READ;
    CHECK(!mm_file_owner_set_flags(&f.state, &f.host, MM_FILE_WRITE | MM_FILE_TEMP, 0xffffffffUL));
    CHECK(f.opens == 3 && f.creates[1] == 0 && f.creates[2] == 0 && f.maximum == 2);
    CHECK(f.flags[1] == (MM_FILE_DENY_WRITE | MM_FILE_WRITE) && f.flags[2] == (MM_FILE_DENY_WRITE | MM_FILE_MARK));
    CHECK(f.state.open && f.state.handle == f.data && f.state.error == MM_FILE_ERROR_READ);
    CHECK(f.state.flags == (MM_FILE_DENY_WRITE | MM_FILE_MARK));
    CHECK(last(&f, EV_ISSUE)->detail == MM_FILE_ISSUE_PERMISSION && last(&f, EV_ISSUE)->state.open);
    CHECK(mm_file_owner_set_flags(&f.state, &f.host, 0, MM_FILE_MARK)); CHECK(f.state.flags & MM_FILE_MARK);
    mm_file_owner_set_temp(&f.state, &f.host, 1); CHECK(f.state.flags & MM_FILE_TEMP);
    mm_file_owner_set_temp(&f.state, &f.host, 0); CHECK(!(f.state.flags & MM_FILE_TEMP));
    f.plan_count = 5; f.plan[3] = f.plan[4] = 0;
    CHECK(!mm_file_owner_set_flags(&f.state, &f.host, MM_FILE_WRITE, MM_FILE_PERMISSIONS));
    CHECK(!f.state.open && !f.state.handle && f.state.error == MM_FILE_ERROR_CRITICAL);
    CHECK(last(&f, EV_ISSUE)->state.error == MM_FILE_ERROR_CRITICAL);
    CHECK(!mm_file_owner_open(&f.state, &f.host, 1, MM_FILE_WRITE) && f.opens == 5);
    f.count = 0;
    CHECK(mm_file_owner_read(&f.state, &f.host, bytes, 0, 0));
    CHECK(mm_file_owner_write(&f.state, &f.host, bytes, 0, 0));
    CHECK(!count(&f, EV_ENTER) && count(&f, EV_ARGUMENT) == 2 && count(&f, EV_VALIDATE) == 2);
    /* Each primitive failure publishes its original state before escalation. */
    for (i = 0; i < 5; ++i) {
        init(&f, MM_FILE_WRITE); opened(&f); f.length = 6; f.fail = failures[i];
        if (f.fail == FAIL_READ || f.fail == FAIL_SEEK) CHECK(!mm_file_owner_read(&f.state, &f.host, bytes, 1, 0));
        else if (f.fail == FAIL_WRITE) CHECK(!mm_file_owner_write(&f.state, &f.host, bytes, 1, 0));
        else if (f.fail == FAIL_RESIZE) CHECK(!mm_file_owner_resize(&f.state, &f.host, 4));
        else CHECK(mm_file_owner_size(&f.state, &f.host) == 0);
        CHECK(last(&f, EV_ISSUE)->state.error == MM_FILE_ERROR_NONE && !f.depth);
        CHECK(f.state.error == (i == 0 || i == 4 ? MM_FILE_ERROR_SEEK : i == 1 ? MM_FILE_ERROR_READ : MM_FILE_ERROR_WRITE));
        CHECK(f.state.wrote == (i == 2 || i == 3));
    }
    init(&f, MM_FILE_WRITE); opened(&f); f.length = 6; f.state.error = MM_FILE_ERROR_WRITE;
    CHECK(mm_file_owner_read(&f.state, &f.host, bytes, 1, 0));
    CHECK(!mm_file_owner_write(&f.state, &f.host, bytes, 1, 0) && !count(&f, EV_WRITE));
    f.state.error = MM_FILE_ERROR_READ; CHECK(mm_file_owner_size(&f.state, &f.host) == 6);
    CHECK(!mm_file_owner_read(&f.state, &f.host, bytes, 1, 0));
    f.state.error = MM_FILE_ERROR_SEEK; f.count = 0;
    CHECK(mm_file_owner_size(&f.state, &f.host) == 0 && !count(&f, EV_SIZE));
    CHECK(!mm_file_owner_resize(&f.state, &f.host, 2) && !count(&f, EV_SEEK) && !count(&f, EV_RESIZE));
    /* Debug size failure prevents seek/transfer; nonfinal close permits auto-reopen. */
    init(&f, MM_FILE_TEMP | MM_FILE_WRITE); opened(&f); f.host.debug_read = 1; f.fail = FAIL_SIZE;
    CHECK(!mm_file_owner_read(&f.state, &f.host, bytes, 1, 0));
    CHECK(f.state.error == MM_FILE_ERROR_SEEK && !count(&f, EV_SEEK) && !count(&f, EV_READ));
    f.fail = 0; f.state.error = 0; mm_file_owner_close(&f.state, &f.host, 0);
    CHECK(!f.state.open && f.state.ever_open && !count(&f, EV_DELETE));
    CHECK(mm_file_owner_write(&f.state, &f.host, "x", 1, 0) && f.opens == 2);
    f.count = 0; f.fail = FAIL_FLUSH | FAIL_DELETE;
    mm_file_owner_close(&f.state, &f.host, 1);
    CHECK(f.maximum == 2 && !f.depth && !f.state.open && !f.state.handle && f.state.wrote);
    CHECK(count(&f, EV_FLUSH) == 1 && count(&f, EV_CLOSE) == 1 && count(&f, EV_DELETE) == 1 && count(&f, EV_ISSUE) == 1);
    CHECK(last(&f, EV_ISSUE)->detail == MM_FILE_ISSUE_DELETE && !last(&f, EV_ISSUE)->state.open);
    CHECK(f.events[0].kind == EV_VALIDATE && f.events[0].detail == 1);
    CHECK(f.events[2].kind == EV_VALIDATE && f.events[2].detail == 0);
    mm_file_owner_close(&f.state, &f.host, 1); CHECK(count(&f, EV_DELETE) == 2 && count(&f, EV_FLUSH) == 1);
    init(&f, MM_FILE_TEMP); f.plan_count = 1; f.plan[0] = 0;
    CHECK(!mm_file_owner_open(&f.state, &f.host, 0, 0));
    mm_file_owner_close(&f.state, &f.host, 1); CHECK(!count(&f, EV_DELETE) && !f.state.ever_open);
    /* Read-only diagnostics are advisory; primitives still decide write success. */
    init(&f, 0); CHECK(mm_file_owner_open(&f.state, &f.host, 0, 0));
    CHECK(mm_file_owner_write(&f.state, &f.host, "r", 1, 0));
    CHECK(last(&f, EV_ASSERT)->detail == MM_FILE_CHECK_WRITE && f.state.wrote);
    /* Hosts may hold the lock externally and omit every diagnostic hook. */
    f.host.enter = f.host.leave = NULL; f.host.validate = NULL;
    f.host.arguments = NULL; f.host.assertion = NULL; f.host.issue = NULL; f.depth = 1;
    f.fail = FAIL_WRITE;
    CHECK(!mm_file_owner_write(&f.state, &f.host, "x", 1, 0));
    CHECK(f.state.error == MM_FILE_ERROR_WRITE && f.depth == 1);
    puts("C89 file owner: I/O, permissions/recovery, publication, recursive locks, diagnostics and temp lifetime passed");
    return 0;
}
