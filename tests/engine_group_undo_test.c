#include "mm/actor_group_undo.h"
#include "mm/movie_title.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "group undo check %d: %s\n", __LINE__, #c); exit(1); } } while (0)

typedef struct Child { int id, refs, context; } Child;
typedef struct Fixture
{
    MMMovieUndoContext base;
    MMActorMoveGroup *moves;
    MMActorRenameGroup names;
    MMActorMoveGroupServices move_services;
    MMActorRenameGroupServices rename_services;
    Child children[3];
    MMMovieTitle live[4];
    int fail_child, contexts, calls, order[3], mutate_context, append_during_undo;
    int missing, fail_rename, captures, renames;
    int allocations, resize_attempts, fail_resize_from;
    Child *retained_at_resize;
} Fixture;

static void *resize_buffer(void *p, void *data, MMU32 old_size, MMU32 size, int priority)
{
    Fixture *f = (Fixture *)p;
    int allocating = data == NULL;
    void *next;
    (void)old_size;
    (void)priority;
    ++f->resize_attempts;
    if (f->retained_at_resize)
        CHECK(f->retained_at_resize->refs == 2);
    if (f->fail_resize_from && f->resize_attempts >= f->fail_resize_from)
        return NULL;
    next = realloc(data, size);
    if (next && allocating)
        ++f->allocations;
    return next;
}

static void free_buffer(void *p, void *data, MMU32 size)
{
    Fixture *f = (Fixture *)p;
    (void)size;
    CHECK(data && f->allocations > 0);
    --f->allocations;
    free(data);
}

static void retain(void *p, void *item)
{
    (void)p;
    ++((Child *)item)->refs;
}

static void release(void *p, void *item)
{
    (void)p;
    CHECK(((Child *)item)->refs > 1);
    --((Child *)item)->refs;
}

static void child_context(void *p, void *item, void *movie, MMI32 scene, MMI32 frame)
{
    Fixture *f = (Fixture *)p;
    CHECK(f->moves && movie == f && scene == 7 && frame == 13);
    ++f->contexts;
    ((Child *)item)->context = 1;
}

static int undo(void *p, void *item, void *document)
{
    Fixture *f = (Fixture *)p;
    Child *child = (Child *)item;
    CHECK(f->moves && document == f && child->context && f->calls < 3);
    f->order[f->calls++] = child->id;
    if (f->mutate_context)
    {
        f->base.scene = 99;
        f->base.frame = 101;
    }
    if (child->id == f->fail_child)
    {
        mm_actor_move_group_dispose(f->moves, &f->move_services);
        free(f->moves);
        f->moves = NULL;
        return 0;
    }
    if (f->append_during_undo && child->id == 1)
        CHECK(mm_actor_move_group_add(f->moves, &f->children[1], &f->move_services));
    return 1;
}

static int find_name(void *p, void *movie, MMI32 actor, const void **text, MMU32 *count)
{
    Fixture *f = (Fixture *)p;
    CHECK(movie == f && actor > 0 && actor < 4);
    ++f->captures;
    if (actor == f->missing)
        return 0;
    *text = mm_movie_title_text(&f->live[actor]);
    *count = f->live[actor].count;
    return 1;
}

static int rename_actor(void *p, void *movie, MMI32 actor, const void *text, MMU32 count)
{
    Fixture *f = (Fixture *)p;
    CHECK(movie == f && actor > 0 && actor < 4);
    ++f->renames;
    if (actor == f->fail_rename)
        return 0;
    return mm_movie_title_set(&f->live[actor], text, count);
}

static void init(Fixture *f)
{
    int i;
    memset(f, 0, sizeof(*f));
    mm_movie_undo_context_init(&f->base);
    f->base.movie = f;
    f->base.scene = 7;
    f->base.frame = 13;
    f->move_services.context = f;
    f->move_services.retain = retain;
    f->move_services.release = release;
    f->move_services.child_context = child_context;
    f->move_services.undo_child = undo;
    f->rename_services.context = f;
    f->rename_services.find_name = find_name;
    f->rename_services.rename = rename_actor;
    for (i = 0; i < 3; ++i)
    {
        f->children[i].id = i + 1;
        f->children[i].refs = 1;
    }
}

static void init_moves(Fixture *f)
{
    MMStorageAllocator allocator;
    allocator.context = f;
    allocator.resize = resize_buffer;
    allocator.release = free_buffer;
    init(f);
    f->moves = (MMActorMoveGroup *)malloc(sizeof(*f->moves));
    CHECK(f->moves && mm_actor_move_group_init(f->moves, &f->base, &allocator, 1));
    CHECK(!f->resize_attempts && !f->moves->count);
}

static void dispose_moves(Fixture *f)
{
    if (f->moves)
    {
        mm_actor_move_group_dispose(f->moves, &f->move_services);
        CHECK(!f->moves->count && !mm_actor_move_group_child(f->moves, 0));
        free(f->moves);
        f->moves = NULL;
    }
    CHECK(!f->allocations);
}

static void test_moves(void)
{
    Fixture f;
    int fail, i;
    for (fail = 0; fail < 4; ++fail)
    {
        init_moves(&f);
        f.fail_child = fail;
        f.mutate_context = 1;
        for (i = 0; i < 3; ++i)
        {
            CHECK(mm_actor_move_group_add(f.moves, &f.children[i], &f.move_services));
            if (i == 0)
                CHECK(mm_actor_move_group_add(f.moves, NULL, &f.move_services));
        }
        CHECK(f.moves->count == 4 && !mm_actor_move_group_child(f.moves, 1));
        CHECK(mm_actor_move_group_toggle(f.moves, &f, &f.move_services) == !fail);
        CHECK(f.calls == (fail ? fail : 3) && f.contexts == f.calls && f.base.scene == 99);
        CHECK((f.moves == NULL) == (fail != 0));
        for (i = 0; i < f.calls; ++i)
            CHECK(f.order[i] == i + 1);
        dispose_moves(&f);
        for (i = 0; i < 3; ++i)
            CHECK(f.children[i].refs == 1);
    }

    init_moves(&f);
    CHECK(mm_actor_move_group_toggle(f.moves, &f, &f.move_services) && !f.calls);
    f.fail_resize_from = 1;
    f.retained_at_resize = &f.children[0];
    CHECK(!mm_actor_move_group_add(f.moves, &f.children[0], &f.move_services));
    CHECK(f.children[0].refs == 1 && !f.moves->count);
    f.fail_resize_from = 0;
    CHECK(mm_actor_move_group_add(f.moves, &f.children[0], &f.move_services));
    f.retained_at_resize = NULL;
    f.append_during_undo = 1;
    CHECK(mm_actor_move_group_toggle(f.moves, &f, &f.move_services) && f.calls == 2);
    dispose_moves(&f);
}

static void init_names(Fixture *f, unsigned unit)
{
    MMStorageAllocator allocator;
    int i;
    allocator.context = f;
    allocator.resize = resize_buffer;
    allocator.release = free_buffer;
    init(f);
    CHECK(mm_actor_rename_group_init(&f->names, &f->base, unit, 0, &allocator, 1));
    CHECK(!f->resize_attempts && !f->names.names.count);
    for (i = 1; i < 4; ++i)
        CHECK(mm_movie_title_init(&f->live[i], unit));
}

static void dispose_names(Fixture *f)
{
    mm_actor_rename_group_dispose(&f->names);
    CHECK(!f->allocations && !f->names.names.count && !f->names.names.bytes);
}

static void check_saved(Fixture *f, MMU32 index, MMI32 actor, const void *text, MMU32 count)
{
    MMI32 id;
    const void *saved;
    MMU32 size;
    CHECK(mm_rollcall_table_extra(&f->names.names, index, &id) && id == actor);
    CHECK(mm_rollcall_table_name(&f->names.names, index, &saved, &size));
    CHECK(size == count && !memcmp(saved, text, count * f->names.names.unit));
}

static int is_live(const Fixture *f, int actor, const void *text, MMU32 count)
{
    const MMMovieTitle *title = &f->live[actor];
    return title->count == count && !memcmp(mm_movie_title_text(title), text, count * title->unit);
}

static void test_names(void)
{
    Fixture f;
    int i, fail;
    char current[] = {'a', 0, 't'};
    const char saved[] = {'b', 0, 'f'};
    unsigned short wide[255];
    for (fail = 0; fail < 3; ++fail)
    {
        init_names(&f, 1);
        for (i = 1; i < 4; ++i)
        {
            current[0] = (char)('a' + i);
            CHECK(mm_movie_title_set(&f.live[i], current, sizeof(current)));
            CHECK(mm_actor_rename_group_add(&f.names, i, saved, sizeof(saved)));
        }
        if (fail == 1)
            f.missing = 2;
        if (fail == 2)
            f.fail_rename = 2;
        CHECK(mm_actor_rename_group_toggle(&f.names, &f.rename_services) == (fail != 2));
        CHECK(f.captures == (fail == 2 ? 2 : 3) && f.renames == (fail == 0 ? 3 : 2));
        for (i = 1; i < 4; ++i)
        {
            int processed = !fail || i == 1 || (fail == 1 && i == 3);
            current[0] = (char)('a' + i);
            CHECK(is_live(&f, i, processed ? saved : current, sizeof(current)));
            check_saved(&f, (MMU32)(i - 1), i, processed ? current : saved, sizeof(saved));
        }
        if (!fail)
        {
            CHECK(mm_actor_rename_group_toggle(&f.names, &f.rename_services));
            for (i = 1; i < 4; ++i)
            {
                current[0] = (char)('a' + i);
                CHECK(is_live(&f, i, current, sizeof(current)));
            }
        }
        dispose_names(&f);
    }

    init_names(&f, 1);
    /* A failed reserve of the second buffer leaves the first one allocated.
     * The next append can reuse it and only complete rows become visible. */
    f.fail_resize_from = 2;
    CHECK(!mm_actor_rename_group_add(&f.names, 1, saved, sizeof(saved)));
    CHECK(f.allocations == 1 && !f.names.names.count);
    f.fail_resize_from = 0;
    CHECK(mm_actor_rename_group_add(&f.names, 1, saved, sizeof(saved)));
    CHECK(mm_actor_rename_group_add(&f.names, 1, "x", 1));
    CHECK(mm_movie_title_set(&f.live[1], current, sizeof(current)));
    CHECK(mm_actor_rename_group_toggle(&f.names, &f.rename_services));
    CHECK(is_live(&f, 1, "x", 1));
    check_saved(&f, 0, 1, current, sizeof(current));
    check_saved(&f, 1, 1, saved, sizeof(saved));
    dispose_names(&f);

    for (i = 0; i < 255; ++i)
        wide[i] = (unsigned short)(i == 3 ? 0 : 0x3a9 + i);
    for (fail = 0; fail < 2; ++fail)
    {
        init_names(&f, 2);
        CHECK(mm_movie_title_set(&f.live[1], wide, 255));
        CHECK(mm_actor_rename_group_add(&f.names, 1, wide + 2, 5));
        CHECK(mm_actor_rename_group_add(&f.names, 2, wide + 10, 1));
        if (fail)
            f.fail_resize_from = f.resize_attempts + 1;
        CHECK(mm_actor_rename_group_toggle(&f.names, &f.rename_services));
        CHECK(is_live(&f, 1, wide + 2, 5) && is_live(&f, 2, wide + 10, 1));
        check_saved(&f, 0, 1, fail ? wide + 2 : wide, fail ? 5 : 255);
        check_saved(&f, 1, 2, wide, 0);
        if (!fail)
        {
            CHECK(mm_actor_rename_group_toggle(&f.names, &f.rename_services));
            CHECK(is_live(&f, 1, wide, 255));
            CHECK(is_live(&f, 2, wide, 0));
        }
        dispose_names(&f);
    }
}

int main(void)
{
    test_moves();
    test_names();
    puts("composite actor undo C89 checks passed");
    return 0;
}
