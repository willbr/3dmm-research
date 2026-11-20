#include "mm/command_dispatch.h"
#include <stdio.h>
#include <stdlib.h>

static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "command dispatch check failed at line %d\n", line);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(condition) check((condition), __LINE__)

typedef struct Fixture
{
    MMCommandDispatch dispatch;
    MMQueuedCommand current;
    int handlers[4];
    int trace[16], count, finished, released, fail_send, refuse_growth, live_buffers;
} Fixture;

static void *resize(void *context, void *buffer, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Fixture *f = (Fixture *)context;
    void *result;
    (void)old_bytes;
    (void)priority;
    if (f->refuse_growth)
        return 0;
    if (!buffer)
    {
        result = malloc(bytes);
        if (result)
            ++f->live_buffers;
        return result;
    }
    return realloc(buffer, bytes);
}

static void free_buffer(void *context, void *buffer, MMU32 bytes)
{
    Fixture *f = (Fixture *)context;
    (void)bytes;
    free(buffer);
    --f->live_buffers;
}

static int release_group(void *context, void *group)
{
    Fixture *f = (Fixture *)context;
    f->released = f->released * 10 + *(int *)group;
    return 0;
}

static int prepare(void *context)
{
    Fixture *f = (Fixture *)context;
    return mm_commands_take(&f->dispatch, &f->current);
}

static void *target(void *context)
{
    return ((Fixture *)context)->current.target;
}

static int send(void *, void *);
static int finish(void *context)
{
    Fixture *f = (Fixture *)context;
    ++f->finished;
    if (f->current.group)
        release_group(f, f->current.group);
    return 0;
}

static const MMCommandRoute route = {prepare, target, send, finish};

static int send(void *context, void *handler)
{
    Fixture *f = (Fixture *)context;
    int number = *(int *)handler;
    CHECK(f->count < (int)(sizeof(f->trace) / sizeof(f->trace[0])));
    f->trace[f->count++] = number;
    if (f->fail_send)
        return -7;
    if (number == 1)
    {
        CHECK(!mm_commands_route(&f->dispatch, f, &route));
        mm_commands_remove_filter(&f->dispatch, handler, 0);
        CHECK(mm_commands_add_filter(&f->dispatch, &f->handlers[1], 0, MM_COMMAND_ALL));
        f->current.target = 0;
    }
    return number == 4;
}

static int enable(void *context, void *handler, MMU32 *flags)
{
    Fixture *f = (Fixture *)context;
    int number = *(int *)handler;
    CHECK(f->count < (int)(sizeof(f->trace) / sizeof(f->trace[0])));
    CHECK(*flags == 0);
    f->trace[f->count++] = number;
    *flags = 8;
    return number == 4;
}

typedef struct MapEntry
{
    MMCommandBinding binding;
    unsigned char tag[7];
} MapEntry;

static int has_default(const void *entry)
{
    return ((const MapEntry *)entry)->tag[0] != 0;
}

static void test_map(void)
{
    static const MapEntry base[] = {{{19, MM_COMMAND_ALL}, {1}}, {{29, MM_COMMAND_ALL}, {2}},
                                    {{0, MM_COMMAND_ALL}, {3}}};
    static const MapEntry derived[] = {{{19, MM_COMMAND_SELF}, {4}}, {{29, MM_COMMAND_ALL}, {0}},
                                       {{0, MM_COMMAND_UNTARGETED}, {5}}};
    static const MapEntry empty_default[] = {{{0, MM_COMMAND_ALL}, {0}}};
    static const MMCommandMap base_map = {NULL, base, sizeof(*base)};
    static const MMCommandMap derived_map = {&base_map, derived, sizeof(*derived)};
    static const MMCommandMap empty_map = {&derived_map, empty_default, sizeof(*empty_default)};
    CHECK(mm_command_map_find(&empty_map, 19, MM_COMMAND_OTHER, has_default) == &base[0]);
    CHECK(mm_command_map_find(&empty_map, 19, MM_COMMAND_SELF, has_default) == &derived[0]);
    CHECK(mm_command_map_find(&empty_map, 29, MM_COMMAND_OTHER, has_default) == &derived[1]);
    CHECK(mm_command_map_find(&empty_map, 33, MM_COMMAND_UNTARGETED, has_default) == &derived[2]);
    CHECK(mm_command_map_find(&empty_map, 33, MM_COMMAND_SELF, has_default) == &base[2]);
    CHECK(mm_command_map_find(&empty_map, 33, 0, has_default) == NULL);
    CHECK(mm_command_binding_enable(&derived[1].binding, 0, 1) == MM_COMMAND_ENABLE_NONE);
    CHECK(mm_command_binding_enable(&derived[0].binding, 1, 0) == MM_COMMAND_ENABLE_EXPLICIT);
    CHECK(mm_command_binding_enable(&derived[2].binding, 1, 0) == MM_COMMAND_ENABLE_NONE);
    CHECK(mm_command_binding_enable(&base[2].binding, 1, 1) == MM_COMMAND_ENABLE_CALLBACK);
}

typedef struct IdProbe
{
    MMI32 occupied, ids[8];
    int count, error;
} IdProbe;

static int id_in_use(void *context, MMI32 id)
{
    IdProbe *probe = (IdProbe *)context;
    CHECK(probe->count < (int)(sizeof(probe->ids) / sizeof(probe->ids[0])));
    probe->ids[probe->count++] = id;
    return probe->error ? probe->error : id == probe->occupied;
}

static void test_ids(void)
{
    IdProbe probe = {0};
    MMU32 last = 0;
    MMI32 first = 77;
    CHECK(!mm_command_id_allocate(&last, 0, &probe, id_in_use, &first));
    CHECK(!last && first == 77 && !probe.count);

    probe.occupied = -2147483646L;
    CHECK(mm_command_id_allocate(&last, 2, &probe, id_in_use, &first) == 1);
    CHECK(first == -2147483645L && last == 0x80000004UL && probe.count == 4);
    CHECK(probe.ids[0] == -2147483647L && probe.ids[1] == probe.occupied);
    CHECK(probe.ids[2] == first && probe.ids[3] == first + 1);

    last = 0xfffffffeUL;
    probe.occupied = 0;
    probe.count = 0;
    CHECK(mm_command_id_allocate(&last, 2, &probe, id_in_use, &first) == 1);
    CHECK(first == -2147483647L && last == 0x80000002UL && probe.count == 3);
    CHECK(probe.ids[0] == -1 && probe.ids[1] == first && probe.ids[2] == first + 1);

    probe.error = -17;
    first = 77;
    CHECK(mm_command_id_allocate(&last, 1, &probe, id_in_use, &first) == -17);
    CHECK(probe.count == 4 && last == 0x80000003UL && first == 77);
    CHECK(mm_command_id_allocate(&last, 1, NULL, NULL, &first) == 1 && first == -2147483644L);
}

int main(void)
{
    Fixture f = {0};
    MMStorageAllocator allocator = {0, resize, free_buffer};
    MMCommandEnable query = {target, enable};
    MMQueuedCommand command = {0}, removed;
    MMU32 flags;
    int first_group = 1, second_group = 2, dispatch_group = 3, close_group = 4;

    test_map();
    test_ids();
    allocator.context = &f;
    CHECK(mm_commands_init(&f.dispatch, &allocator, 1));
    f.refuse_growth = 1;
    CHECK(!mm_commands_reserve(&f.dispatch, 1, 1));
    CHECK(!mm_commands_insert(&f.dispatch, &command, 0));
    CHECK(!mm_commands_add_filter(&f.dispatch, &f.handlers[0], 0, MM_COMMAND_ALL));
    CHECK(mm_commands_take_key(&f.dispatch, 2, &removed) == 0);
    f.refuse_growth = 0;

    f.handlers[0] = 1;
    f.handlers[1] = 2;
    f.handlers[2] = 3;
    f.handlers[3] = 4;
    CHECK(mm_commands_add_filter(&f.dispatch, &f.handlers[0], 0, MM_COMMAND_ALL));
    CHECK(mm_commands_add_filter(&f.dispatch, &f.handlers[2], 10, MM_COMMAND_OTHER));
    CHECK(mm_commands_add_filter(&f.dispatch, &f.handlers[3], 10, MM_COMMAND_UNTARGETED));
    command.id = 1;
    command.group = &first_group;
    command.target = &f.handlers[2];
    CHECK(mm_commands_insert(&f.dispatch, &command, 0));
    command.group = &second_group;
    CHECK(mm_commands_insert(&f.dispatch, &command, 0));
    command.id = 2;
    command.group = &dispatch_group;
    CHECK(mm_commands_insert(&f.dispatch, &command, 1));
    CHECK(mm_commands_take_key(&f.dispatch, 3, &removed) == -1);
    CHECK(mm_commands_route(&f.dispatch, &f, &route) == 1);
    CHECK(f.count == 2 && f.trace[0] == 1 && f.trace[1] == 4);
    CHECK(f.finished == 1 && f.released == 3);

    CHECK(mm_commands_contains(&f.dispatch, 1));
    CHECK(mm_commands_flush(&f.dispatch, 1, &f, release_group) == 0);
    CHECK(f.released == 312 && !mm_commands_contains(&f.dispatch, 1));
    f.count = 0;
    CHECK(mm_commands_enable(&f.dispatch, &f, &query, &flags) == 1 && flags == 8);
    CHECK(f.count == 2 && f.trace[0] == 2 && f.trace[1] == 4);

    command.group = 0;
    CHECK(mm_commands_insert(&f.dispatch, &command, 0));
    CHECK(mm_commands_take_key(&f.dispatch, 2, &removed) == 1 && removed.id == 2);
    CHECK(mm_commands_insert(&f.dispatch, &command, 0));
    f.fail_send = 1;
    CHECK(mm_commands_route(&f.dispatch, &f, &route) == -7);
    CHECK(f.finished == 1 && !f.dispatch.dispatching);
    CHECK(mm_commands_route(&f.dispatch, &f, &route) == 0);
    command.group = &first_group;
    CHECK(mm_commands_insert(&f.dispatch, &command, 0));
    CHECK(mm_commands_bury(&f.dispatch, &f.handlers[2], &f, release_group) == 0);
    CHECK(f.released == 3121 && f.dispatch.filters.count == 2);
    command.target = 0;
    command.group = &close_group;
    CHECK(mm_commands_insert(&f.dispatch, &command, 0));
    CHECK(mm_commands_dispose(&f.dispatch, &f, release_group) == 0);
    CHECK(f.released == 31214 && f.live_buffers == 0);
    return EXIT_SUCCESS;
}
