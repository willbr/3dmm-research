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

int main(void)
{
    Fixture f = {0};
    MMStorageAllocator allocator = {0, resize, free_buffer};
    MMCommandEnable query = {target, enable};
    MMQueuedCommand command = {0}, removed;
    MMU32 flags;
    int first_group = 1, second_group = 2, dispatch_group = 3, close_group = 4;

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
