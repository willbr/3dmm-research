#include "mm/file_ops.h"
#include "mm/linked_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{ if (!value) { fprintf(stderr, "File operations check failed at %d\n", line); exit(1); } }
#define CHECK(x) check(!!(x), __LINE__)
enum { ENTER_LIST = 1, LEAVE_LIST, ENTER_OBJECT, LEAVE_OBJECT, CONSTRUCT, DESTROY, RETAIN,
       VALIDATE_NAME, VALIDATE_OBJECT, COPY, UNIQUE, TEMP, SAME_DIR, RENAME, STATUS, DELETE_NAME,
       ISSUE, ASSERTION, OPEN_OWNER, CLOSE_OWNER, FLUSH_OWNER, DELETE_OWNER, OWNER_ISSUE };
typedef struct Name { int id, directory; } Name;
typedef struct Fixture Fixture;
typedef struct Object {
    Fixture *fixture;
    int used, index, depth;
    Name name;
    MMFileOwner file;
    MMReference refs;
    MMLinkedListNode link;
} Object;
typedef struct Event {
    int kind, object, a, b, list_depth, object_depth;
    int name_a, name_b, open, error;
    MMU32 flags;
} Event;
struct Fixture {
    MMFileOpsHost host;
    MMLinkedListNode *head;
    Object objects[8];
    int physical[16], list_depth, events, constructed, destroyed, unique_fail, temp_fail, construct_fail;
    int renames, opens, same_dirs, status_override, delete_fail;
    unsigned rename_fail, open_fail;
    Event log[1024];
};
static Object *object(void *p)
{ Object *o = (Object *)p; CHECK(o && o->used); return o; }
static void record(Fixture *f, int kind, Object *o, int a, int b)
{
    Event *e;
    CHECK(f->events < 1024); e = &f->log[f->events++]; memset(e, 0, sizeof(*e));
    e->kind = kind; e->object = o ? o->index : -1; e->a = a; e->b = b;
    e->list_depth = f->list_depth; e->name_a = f->objects[0].name.id; e->name_b = f->objects[1].name.id;
    if (o) { e->object_depth = o->depth; e->open = o->file.open; e->error = o->file.error; e->flags = o->file.flags; }
}
static void enter_list(void *c) { Fixture *f = (Fixture *)c; ++f->list_depth; record(f, ENTER_LIST, NULL, 0, 0); }
static void leave_list(void *c) { Fixture *f = (Fixture *)c; CHECK(f->list_depth > 0); record(f, LEAVE_LIST, NULL, 0, 0); --f->list_depth; }
static void *first(void *c) { Fixture *f = (Fixture *)c; return f->head ? f->head->owner : NULL; }
static void *next(void *c, void *p)
{ Object *o = object(p); (void)c; return o->link.next ? o->link.next->owner : NULL; }
static void enter_object(void *c, void *p)
{ Object *o = object(p); ++o->depth; record((Fixture *)c, ENTER_OBJECT, o, 0, 0); }
static void leave_object(void *c, void *p)
{ Object *o = object(p); CHECK(o->depth > 0); record((Fixture *)c, LEAVE_OBJECT, o, 0, 0); --o->depth; }
static void *name(void *c, void *p) { (void)c; return &object(p)->name; }
static MMFileOwner *owner(void *c, void *p) { (void)c; return &object(p)->file; }
static MMReference *references(void *c, void *p) { (void)c; return &object(p)->refs; }
static void owner_enter(void *p) { Object *o = object(p); enter_object(o->fixture, p); }
static void owner_leave(void *p) { Object *o = object(p); leave_object(o->fixture, p); }
static void *raw_open(void *p, int create, MMU32 flags)
{
    Object *o = object(p); Fixture *f = o->fixture;
    int call = f->opens++;
    CHECK(o->depth > 0 && call < 32); record(f, OPEN_OWNER, o, create, (int)flags);
    if (f->open_fail & (1U << call)) return NULL;
    if (create) {
        if ((flags & MM_FILE_TEMP) && f->physical[o->name.id]) return NULL;
        f->physical[o->name.id] = 1000 + o->index;
    } else if (!f->physical[o->name.id]) return NULL;
    return o;
}
static void raw_close(void *p, void *handle)
{ Object *o = object(p); CHECK(o->depth > 0 && handle == o && o->file.open); record(o->fixture, CLOSE_OWNER, o, 0, 0); }
static int raw_flush(void *p, void *handle)
{ Object *o = object(p); CHECK(o->depth > 0 && handle == o); record(o->fixture, FLUSH_OWNER, o, 0, 0); return 1; }
static int raw_remove(void *p)
{
    Object *o = object(p); Fixture *f = o->fixture; int existed = f->physical[o->name.id] != 0;
    CHECK(o->depth > 0 && !o->file.open); record(f, DELETE_OWNER, o, 0, 0); f->physical[o->name.id] = 0; return existed;
}
static void owner_issue(void *p, int code)
{ Object *o = object(p); record(o->fixture, OWNER_ISSUE, o, code, 0); }
static void owner_host(void *c, void *p, MMFileOwnerHost *out)
{
    (void)c; object(p); memset(out, 0, sizeof(*out)); out->context = p;
    out->enter = owner_enter; out->leave = owner_leave; out->open = raw_open;
    out->close = raw_close; out->flush = raw_flush; out->remove = raw_remove; out->issue = owner_issue;
}
static int equal(void *c, void *a, void *b)
{ Name *x = (Name *)a, *y = (Name *)b; (void)c; return x->id == y->id && x->directory == y->directory; }
static void copy_name(void *c, void *a, const void *b)
{ record((Fixture *)c, COPY, NULL, ((Name *)a)->id, ((const Name *)b)->id); *(Name *)a = *(const Name *)b; }
static int unique(void *c, void *p)
{ Fixture *f = (Fixture *)c; record(f, UNIQUE, NULL, ((Name *)p)->id, 0); if (f->unique_fail) return 0; ((Name *)p)->id = 3; return 1; }
static int temp(void *c, void *p)
{ Fixture *f = (Fixture *)c; record(f, TEMP, NULL, 0, 0); if (f->temp_fail) return 0; ((Name *)p)->id = 4; ((Name *)p)->directory = 0; return 1; }
static int same_directory(void *c, void *a, void *b)
{
    Fixture *f = (Fixture *)c; ++f->same_dirs; record(f, SAME_DIR, NULL, 0, 0);
    return ((Name *)a)->directory == ((Name *)b)->directory;
}
static int rename_name(void *c, void *a, void *b)
{
    Fixture *f = (Fixture *)c; Name *x = (Name *)a, *y = (Name *)b;
    int call = f->renames++;
    CHECK(call < 32); record(f, RENAME, NULL, x->id, y->id);
    if ((f->rename_fail & (1U << call)) || !f->physical[x->id] || f->physical[y->id]) return 0;
    f->physical[y->id] = f->physical[x->id]; f->physical[x->id] = 0; return 1;
}
static int status(void *c, void *p)
{
    Fixture *f = (Fixture *)c; record(f, STATUS, NULL, ((Name *)p)->id, 0);
    return f->status_override ? f->status_override : f->physical[((Name *)p)->id] != 0;
}
static int delete_name(void *c, void *p)
{
    Fixture *f = (Fixture *)c; record(f, DELETE_NAME, NULL, ((Name *)p)->id, 0);
    if (f->delete_fail) return 0;
    f->physical[((Name *)p)->id] = 0; return 1;
}
static void *construct(void *c, void *p, MMU32 flags)
{
    Fixture *f = (Fixture *)c; Object *o; int i;
    ++f->constructed; record(f, CONSTRUCT, NULL, ((Name *)p)->id, (int)flags);
    if (f->construct_fail) return NULL;
    for (i = 0; i < 8; ++i) if (!f->objects[i].used) break;
    CHECK(i < 8); o = &f->objects[i]; memset(o, 0, sizeof(*o));
    o->used = 1; o->index = i; o->fixture = f; o->name = *(Name *)p;
    mm_file_owner_init(&o->file, flags); mm_reference_init(&o->refs); mm_linked_list_init(&o->link, o);
    enter_list(f); mm_linked_list_attach(&o->link, &f->head, NULL); leave_list(f); return o;
}
static void retain(void *c, void *p)
{ Object *o = object(p); record((Fixture *)c, RETAIN, o, o->refs.count, 0); mm_reference_add(&o->refs); }
static void destroy(void *c, void *p)
{
    Fixture *f = (Fixture *)c; Object *o = object(p); MMFileOwnerHost host;
    record(f, DESTROY, o, 0, 0); owner_host(c, p, &host); mm_file_owner_close(&o->file, &host, 1);
    enter_list(f); mm_linked_list_attach(&o->link, NULL, NULL); leave_list(f);
    CHECK(!o->depth); o->used = 0; memset(&o->file, 0xdd, sizeof(o->file)); ++f->destroyed;
}
static void validate_name(void *c, void *p) { CHECK(p); record((Fixture *)c, VALIDATE_NAME, NULL, ((Name *)p)->id, 0); }
static void validate_object(void *c, void *p) { record((Fixture *)c, VALIDATE_OBJECT, object(p), 0, 0); }
static void issue(void *c, int code) { record((Fixture *)c, ISSUE, NULL, code, 0); }
static void assertion(void *c, int code) { record((Fixture *)c, ASSERTION, NULL, code, 0); }
static int swap_objects(void *c, void *a, void *b)
{
    Fixture *f = (Fixture *)c; Name scratch;
    memset(&scratch, 0, sizeof(scratch)); validate_object(c, a); validate_object(c, b);
    return mm_file_ops_swap(&f->host, a, b, &scratch);
}
static int rename_object(void *c, void *p, void *n)
{ Fixture *f = (Fixture *)c; validate_object(c, p); validate_name(c, n); return mm_file_ops_rename(&f->host, p, n); }
static void init(Fixture *f)
{
    MMFileOpsHost *h;
    memset(f, 0, sizeof(*f)); h = &f->host; h->context = f;
    h->enter_list = enter_list; h->leave_list = leave_list; h->first = first; h->next = next;
    h->enter_object = enter_object; h->leave_object = leave_object; h->name = name; h->owner = owner;
    h->references = references; h->owner_host = owner_host; h->equal_names = equal; h->copy_name = copy_name;
    h->unique_name = unique; h->temp_name = temp; h->same_directory = same_directory;
    h->rename_name = rename_name; h->name_status = status; h->delete_name = delete_name;
    h->construct = construct; h->retain = retain; h->destroy = destroy;
    h->swap_objects = swap_objects; h->rename_object = rename_object;
    h->validate_name = validate_name; h->validate_object = validate_object; h->issue = issue; h->assertion = assertion;
}
static Object *existing(Fixture *f, int id, int content)
{
    Name n; Object *o;
    n.id = id; n.directory = 0; f->physical[id] = content;
    o = (Object *)mm_file_ops_open(&f->host, &n, MM_FILE_WRITE | MM_FILE_DENY_WRITE); CHECK(o); return o;
}
static int count(Fixture *f, int kind, int detail)
{
    int i, result = 0;
    for (i = 0; i < f->events; ++i) if (f->log[i].kind == kind && (detail < 0 || f->log[i].a == detail)) ++result;
    return result;
}
static Event *last(Fixture *f, int kind)
{
    int i;
    for (i = f->events; i-- > 0;) if (f->log[i].kind == kind) return &f->log[i];
    CHECK(0); return NULL;
}
static void unlocked(Fixture *f)
{ int i; CHECK(!f->list_depth); for (i = 0; i < 8; ++i) CHECK(!f->objects[i].depth); }
int main(void)
{
    Fixture f, independent;
    Object *a, *b, *p;
    Name n, scratch;
    int mask, result, i, leave_ids[2], leaves, recovery = 0;
    memset(&scratch, 0, sizeof(scratch)); n.id = 1; n.directory = 0;
    init(&f); CHECK(!mm_file_ops_find(&f.host, &n));
    a = (Object *)mm_file_ops_create(&f.host, &n, 0); CHECK(a && (a->file.flags & MM_FILE_WRITE));
    CHECK(mm_file_ops_find(&f.host, &n) == a && a->refs.count == 1);
    CHECK(mm_file_ops_open(&f.host, &n, 0) == a && a->refs.count == 2 && f.constructed == 1);
    CHECK(!mm_file_ops_create(&f.host, &n, 0));
    CHECK(count(&f, ASSERTION, MM_FILE_OP_CREATE_OPEN) == 1 && !count(&f, ISSUE, MM_FILE_OP_CREATE));
    init(&independent); p = existing(&independent, 1, 303);
    CHECK(mm_file_ops_find(&independent.host, &n) == p && mm_file_ops_find(&f.host, &n) == a);
    n.id = 9; f.events = 0;
    CHECK(!mm_file_ops_open(&f.host, &n, 0)); CHECK(f.destroyed == 1 && last(&f, ISSUE)->a == MM_FILE_OP_OPEN);
    CHECK(last(&f, ISSUE)->list_depth == 0 && count(&f, DESTROY, -1) == 1);
    f.construct_fail = 1; f.events = 0;
    CHECK(!mm_file_ops_create(&f.host, &n, 0)); CHECK(count(&f, ISSUE, MM_FILE_OP_CREATE) == 1 && !count(&f, DESTROY, -1));
    f.construct_fail = 0; f.unique_fail = 1; f.events = 0;
    CHECK(!mm_file_ops_temp(&f.host, &n, &scratch) && !count(&f, CONSTRUCT, -1));
    CHECK(count(&f, ISSUE, MM_FILE_OP_CREATE) == 1);
    f.unique_fail = 0; p = (Object *)mm_file_ops_temp(&f.host, &n, &scratch);
    CHECK(p && p->name.id == 3 && (p->file.flags & MM_FILE_TEMP) && (p->file.flags & MM_FILE_DENY_WRITE));
    f.temp_fail = 1; CHECK(!mm_file_ops_temp(&f.host, NULL, &scratch)); f.temp_fail = 0;
    CHECK(mm_file_ops_temp(&f.host, NULL, &scratch) && scratch.id == 4); unlocked(&f);
    init(&f); a = existing(&f, 1, 101); f.events = 0; n.id = 1;
    f.open_fail = 1U << f.opens;
    CHECK(!mm_file_ops_open(&f.host, &n, MM_FILE_WRITE | MM_FILE_DENY_READ));
    CHECK(a->refs.count == 1 && a->file.open && !a->file.error);
    CHECK(count(&f, OWNER_ISSUE, MM_FILE_ISSUE_PERMISSION) == 1 && !count(&f, ISSUE, MM_FILE_OP_OPEN));
    f.open_fail = 0;
    CHECK(mm_file_ops_open(&f.host, &n, MM_FILE_WRITE | MM_FILE_DENY_READ) == a && a->refs.count == 2);
    CHECK(mm_file_ops_open(&f.host, &n, MM_FILE_TEMP) == a);
    CHECK(count(&f, ASSERTION, MM_FILE_OP_TEMP_OPEN) == 1);
    n.id = 5; f.physical[5] = 909; f.events = 0;
    CHECK(!mm_file_ops_create(&f.host, &n, MM_FILE_TEMP));
    CHECK(f.physical[5] == 909 && f.destroyed == 1 && !count(&f, DELETE_OWNER, -1));
    CHECK(last(&f, ISSUE)->a == MM_FILE_OP_CREATE);
    f.construct_fail = 1; n.id = 9; f.events = 0;
    CHECK(!mm_file_ops_open(&f.host, &n, 0)); CHECK(count(&f, ISSUE, MM_FILE_OP_OPEN) == 1 && !count(&f, DESTROY, -1));
    /* Exhaust all failure combinations in the three moves and two rollback legs.
     * Logical owner names must always identify their surviving physical data. */
    for (mask = 0; mask < 32; ++mask) {
        init(&f); a = existing(&f, 1, 101); b = existing(&f, 2, 202);
        f.events = f.opens = 0; f.rename_fail = (unsigned)mask;
        result = mm_file_ops_swap(&f.host, a, b, &scratch);
        CHECK(result == ((mask & 7) == 0));
        CHECK(f.physical[a->name.id] == 101 && f.physical[b->name.id] == 202);
        CHECK(a->file.open && b->file.open && f.opens == 2);
        CHECK(count(&f, ISSUE, MM_FILE_OP_SWAP) == !result);
        CHECK(last(&f, VALIDATE_OBJECT)->object == b->index);
        leaves = 0;
        for (i = f.events; i-- > 0 && leaves < 2;) if (f.log[i].kind == LEAVE_OBJECT) leave_ids[leaves++] = f.log[i].object;
        CHECK(leaves == 2 && leave_ids[0] == b->index && leave_ids[1] == a->index);
        recovery += count(&f, ASSERTION, MM_FILE_OP_RECOVERY); unlocked(&f);
    }
    CHECK(recovery > 0);
    init(&f); a = existing(&f, 1, 101); b = existing(&f, 2, 202); f.events = f.opens = 0;
    CHECK(mm_file_ops_swap(&f.host, a, a, &scratch) && !count(&f, ENTER_OBJECT, -1));
    f.unique_fail = 1; f.events = 0;
    CHECK(!mm_file_ops_swap(&f.host, a, b, &scratch) && !count(&f, CLOSE_OWNER, -1) && !f.opens);
    f.unique_fail = 0; f.open_fail = 3; f.events = 0;
    CHECK(mm_file_ops_swap(&f.host, a, b, &scratch));
    CHECK(!a->file.open && !b->file.open && a->file.error == MM_FILE_ERROR_CRITICAL && b->file.error == MM_FILE_ERROR_CRITICAL);
    CHECK(!count(&f, ISSUE, MM_FILE_OP_SWAP)); unlocked(&f);
    init(&f); a = existing(&f, 1, 101); n.id = 3; f.events = f.opens = 0; f.rename_fail = 1;
    CHECK(!mm_file_ops_rename(&f.host, a, &n) && a->name.id == 1 && a->file.open);
    CHECK(last(&f, ISSUE)->a == MM_FILE_OP_RENAME && last(&f, ISSUE)->object_depth == 0);
    f.rename_fail = 0; f.open_fail = 1U << f.opens;
    CHECK(!mm_file_ops_rename(&f.host, a, &n) && a->name.id == 3 && f.physical[3] == 101 && !a->file.open);
    init(&f); a = existing(&f, 1, 101); b = existing(&f, 2, 202); n.id = 1; f.events = 0;
    CHECK(mm_file_ops_set_name(&f.host, a, &n) && !count(&f, CLOSE_OWNER, -1));
    n.id = 2; CHECK(mm_file_ops_set_name(&f.host, a, &n));
    CHECK(a->name.id == 2 && b->name.id == 1 && (b->file.flags & MM_FILE_TEMP));
    n.id = 5; f.status_override = 2; f.delete_fail = 1; f.events = 0;
    CHECK(mm_file_ops_set_name(&f.host, a, &n));
    CHECK(count(&f, DELETE_NAME, -1) == 1 && a->name.id == 5 && f.physical[5] == 101); unlocked(&f);
    n.id = 6; f.physical[6] = 606; f.events = 0;
    CHECK(!mm_file_ops_set_name(&f.host, a, &n));
    CHECK(a->name.id == 5 && f.physical[5] == 101 && f.physical[6] == 606);
    f.delete_fail = 0; CHECK(mm_file_ops_set_name(&f.host, a, &n) && f.physical[6] == 101);
    init(&f); a = existing(&f, 1, 101); b = existing(&f, 2, 202); n.id = 2; f.rename_fail = 1;
    CHECK(!mm_file_ops_set_name(&f.host, a, &n) && !(b->file.flags & MM_FILE_TEMP));
    f.rename_fail = 0; f.renames = 0; n.id = 5; n.directory = 1;
    CHECK(mm_file_ops_rename(&f.host, a, &n));
    CHECK(count(&f, ASSERTION, MM_FILE_OP_RENAME_DIRECTORY) == 1);
    /* Destruction poisons state; any traversal using a destroyed owner fails. */
    init(&f); a = existing(&f, 1, 101); b = existing(&f, 2, 202); p = existing(&f, 3, 303);
    a->refs.count = b->refs.count = p->refs.count = 0; b->file.flags |= MM_FILE_MARK | MM_FILE_TEMP;
    mm_file_ops_clear_marks(&f.host); CHECK(b->file.flags & MM_FILE_MARK);
    mm_file_ops_close_unmarked(&f.host); CHECK(f.destroyed == 2 && first(&f) == b && !next(&f, b));
    CHECK(mm_file_ops_contains(&f.host, b) && !mm_file_ops_contains(&f.host, a) && !mm_file_ops_contains(&f.host, NULL));
    mm_file_ops_shutdown(&f.host); CHECK(b->used && !b->file.open && f.physical[2] == 0);
    mm_file_ops_shutdown(&f.host); CHECK(count(&f, OWNER_ISSUE, MM_FILE_ISSUE_DELETE) == 1); unlocked(&f);
    init(&f); a = existing(&f, 1, 101); b = existing(&f, 2, 202);
    f.host.assertion = NULL; f.host.validate_name = NULL; f.host.validate_object = NULL; f.host.issue = NULL;
    CHECK(mm_file_ops_swap(&f.host, a, b, &scratch) && f.same_dirs == 0); unlocked(&f);
    puts("C89 file operations: factories, registries, all swap recovery legs, rename/reopen and publication passed");
    return 0;
}
