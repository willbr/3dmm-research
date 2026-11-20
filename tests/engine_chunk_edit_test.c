#include "mm/chunk_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{
    if (!value)
    {
        fprintf(stderr, "C89 chunk edit failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Node
{
    MMChunkEntry entry;
    int runtime;
    char name[16];
    MMChunkChild children[4];
} Node;
typedef struct Fixture
{
    Node nodes[32];
    MMU32 count;
    MMChunkEdit edit;
    MMStorageAllocator walk_memory;
    MMChunkLocation output;
    MMChunkLocation freed[32];
    int frees, resets, clears, outputs, extra, fail;
    int unreadable_entry, unreadable_child;
    int file, output_added, walk_allocations, live_walk_buffers, issues[5];
    char log[256];
    int logs;
    MMU32 stored_numbers[32];
    int stores;
} Fixture;
static void log_event(Fixture *f, char ch)
{
    CHECK(f->logs < 255);
    f->log[f->logs++] = ch;
}
static MMU32 count(void *ctx)
{
    return ((Fixture *)ctx)->count;
}
static int entry(void *ctx, MMU32 i, MMChunkEntry *entry)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(i < f->count);
    if (f->unreadable_entry)
        return 0;
    *entry = f->nodes[i].entry;
    return 1;
}
static int child(void *ctx, MMU32 p, MMU32 i, MMChunkChild *out)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(p < f->count && i < f->nodes[p].entry.child_count && i < 4);
    if (f->unreadable_child)
        return 0;
    *out = f->nodes[p].children[i];
    return 1;
}
static int insert(void *ctx, MMU32 i)
{
    Fixture *f = (Fixture *)ctx;
    log_event(f, 'I');
    CHECK(i <= f->count && f->count < 32);
    if (f->fail == 1)
        return 0;
    memmove(f->nodes + i + 1, f->nodes + i, (f->count - i) * sizeof(Node));
    memset(f->nodes + i, 0, sizeof(Node));
    ++f->count;
    return 1;
}
static void store(void *ctx, MMU32 i, const MMChunkEntry *record, int initialize)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(i < f->count);
    log_event(f, 'S');
    if (initialize)
        memset(f->nodes + i, 0, sizeof(Node));
    f->nodes[i].entry = *record;
    CHECK(f->stores < 32);
    f->stored_numbers[f->stores++] = record->number;
}
static void erase(void *ctx, MMU32 i)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(i < f->count);
    log_event(f, 'E');
    --f->count;
    memmove(f->nodes + i, f->nodes + i + 1, (f->count - i) * sizeof(Node));
}
static int allocate(void *ctx, MMU32 bytes, MMChunkLocation *out)
{
    Fixture *f = (Fixture *)ctx;
    log_event(f, 'A');
    if (f->fail == 2)
        return 0;
    memset(out, 0, sizeof(*out));
    out->file = &f->file;
    out->offset = bytes ? 200 : 0;
    out->bytes = bytes;
    out->flags = bytes && f->extra ? 1 : 0;
    return 1;
}
static void free_space(void *ctx, int extra, MMU32 offset, MMU32 bytes)
{
    Fixture *f = (Fixture *)ctx;
    MMChunkLocation *out;
    log_event(f, 'F');
    CHECK(f->frees < 32);
    out = f->freed + f->frees++;
    memset(out, 0, sizeof(*out));
    out->offset = offset;
    out->bytes = bytes;
    out->flags = (MMU32)extra;
}
static int write(void *ctx, const MMChunkLocation *where, int added)
{
    Fixture *f = (Fixture *)ctx;
    log_event(f, 'W');
    CHECK(f->outputs == 1 && f->output.offset == where->offset && f->output_added == added);
    return f->fail != 3;
}
static void output(void *ctx, const MMChunkLocation *where, int added)
{
    Fixture *f = (Fixture *)ctx;
    f->output_added = added;
    if (where)
    {
        f->output = *where;
        ++f->outputs;
        log_event(f, 'O');
    }
    else
    {
        memset(&f->output, 0, sizeof(f->output));
        ++f->clears;
        log_event(f, 'C');
    }
}
static void reset(void *ctx, MMU32 type, MMU32 number)
{
    Fixture *f = (Fixture *)ctx;
    MMU32 i;
    log_event(f, 'R');
    ++f->resets;
    for (i = 0; i < f->count; ++i)
        if (f->nodes[i].entry.type == type && f->nodes[i].entry.number == number)
            f->nodes[i].runtime = 0;
}
static void *walk_resize(void *p, void *data, MMU32 old_bytes, MMU32 size, int priority)
{
    Fixture *f = p;
    const int fresh = data == NULL;
    void *buffer;
    (void)old_bytes;
    (void)priority;
    ++f->walk_allocations;
    if (f->fail == 5)
        return NULL;
    buffer = realloc(data, size);
    if (buffer && fresh)
        ++f->live_walk_buffers;
    return buffer;
}
static void walk_release(void *p, void *data, MMU32 bytes)
{
    Fixture *f = p;
    (void)bytes;
    CHECK(f->live_walk_buffers > 0);
    --f->live_walk_buffers;
    free(data);
}
static void issue(void *ctx, int kind)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(kind >= 0 && kind < 5);
    ++f->issues[kind];
}
static int insert_child(void *context, MMU32 parent, MMU32 i, const MMChunkChild *child)
{
    Fixture *f = (Fixture *)context;
    Node *n = f->nodes + parent;
    MMU32 count = n->entry.child_count;
    CHECK(parent < f->count && i <= count && count < 4);
    if (f->fail == 4)
        return 0;
    memmove(n->children + i + 1, n->children + i, (count - i) * sizeof(*child));
    n->children[i] = *child;
    n->entry.variable_bytes += 12;
    return 1;
}
static void erase_child(void *context, MMU32 parent, MMU32 i)
{
    Fixture *f = (Fixture *)context;
    Node *n = f->nodes + parent;
    MMU32 count = n->entry.child_count;
    CHECK(parent < f->count && i <= count && count < 4);
    memmove(n->children + i, n->children + i + 1, (count - i) * sizeof(MMChunkChild));
    memset(n->children + count, 0, sizeof(MMChunkChild));
    n->entry.variable_bytes -= 12;
}
static void put_child(void *context, MMU32 parent, MMU32 i, const MMChunkChild *child)
{
    Fixture *f = (Fixture *)context;
    CHECK(parent < f->count && i < f->nodes[parent].entry.child_count);
    f->nodes[parent].children[i] = *child;
}
static void init(Fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->output.offset = 999;
    f->edit.context = f;
    f->edit.payload_context = f;
    f->edit.max_references = 3;
    f->walk_memory.context = f;
    f->walk_memory.resize = walk_resize;
    f->walk_memory.release = walk_release;
    f->edit.walk_allocator = &f->walk_memory;
    f->edit.count = count;
    f->edit.entry = entry;
    f->edit.child = child;
    f->edit.insert = insert;
    f->edit.store = store;
    f->edit.erase = erase;
    f->edit.allocate = allocate;
    f->edit.free_space = free_space;
    f->edit.write = write;
    f->edit.output = output;
    f->edit.reset_runtime = reset;
    f->edit.issue = issue;
    f->edit.insert_child = insert_child;
    f->edit.erase_child = erase_child;
    f->edit.put_child = put_child;
}
static void node(Fixture *f, unsigned i, MMU32 number, MMU32 flags, MMU32 parents)
{
    Node *n = f->nodes + i;
    n->entry.type = 10;
    n->entry.number = number;
    n->entry.flags = flags;
    n->entry.parent_count = parents;
    n->entry.offset = 128 + i * 10;
    n->entry.bytes = 7;
    n->runtime = 99;
    memcpy(n->name, "retained", sizeof("retained"));
    if (f->count <= i)
        f->count = i + 1;
}
static void put_cases(void)
{
    Fixture f;
    Node old;
    int existing, extra, zero, fail, result, fails;
    for (existing = 0; existing < 2; ++existing)
        for (extra = 0; extra < 2; ++extra)
            for (zero = 0; zero < 2; ++zero)
                for (fail = 0; fail < 4; ++fail)
                {
                    init(&f);
                    f.fail = fail;
                    f.extra = extra;
                    if (existing)
                    {
                        node(&f, 0, 5, 22, 3);
                        f.nodes[0].entry.child_count = 2;
                        f.nodes[0].entry.variable_bytes = 36;
                        f.nodes[0].children[0].number = 8;
                        f.nodes[0].children[1].number = 9;
                    }
                    old = f.nodes[0];
                    fails = fail && (!existing || fail != 1);
                    result = mm_chunk_put(&f.edit, 10, 5, zero ? 0 : 9);
                    CHECK(!strcmp(f.log, !fails      ? (existing ? "AOWFSR" : "IASOW")
                                         : existing  ? (fail == 2 ? "A" : "AOWFC")
                                         : fail == 1 ? "IC"
                                         : fail == 2 ? "IAEC"
                                                     : "IASOWSFREC"));
                    CHECK(result == (!fails      ? MM_CHUNK_OK
                                     : fail == 1 ? MM_CHUNK_MEMORY
                                                 : MM_CHUNK_IO));
                    if (fails)
                    {
                        CHECK(f.count == (MMU32)existing);
                        if (existing)
                            CHECK(!memcmp(f.nodes, &old, sizeof(old)));
                        CHECK(f.clears == (!existing || fail == 3));
                        CHECK(f.output.offset == (existing && fail == 2 ? 999U : 0U));
                        CHECK(f.frees == (fail == 3) && f.resets == (!existing && fail == 3));
                        if (fail == 3)
                            CHECK(f.freed[0].offset == (zero ? 0U : 200U) &&
                                  f.freed[0].flags == (MMU32)(extra && !zero));
                    }
                    else
                    {
                        CHECK(f.count == 1 && f.nodes[0].entry.offset == (zero ? 0U : 200U) && !f.clears);
                        CHECK(f.nodes[0].entry.flags == ((existing ? 22U : 2U) | (extra && !zero ? 1U : 0U)));
                        CHECK(f.nodes[0].runtime == 0 && f.resets == existing && f.frees == existing);
                        if (existing)
                        {
                            CHECK(!strcmp(f.nodes[0].name, old.name) &&
                                  !memcmp(f.nodes[0].children, old.children, sizeof(old.children)));
                            CHECK(f.nodes[0].entry.parent_count == 3 && f.nodes[0].entry.child_count == 2 &&
                                  f.nodes[0].entry.variable_bytes == 36);
                            CHECK(f.freed[0].offset == 128 && f.freed[0].bytes == 7);
                        }
                        else
                            CHECK(!f.nodes[0].entry.parent_count && !f.nodes[0].entry.child_count &&
                                  !f.nodes[0].name[0]);
                    }
                }
    init(&f);
    f.edit.write = NULL;
    CHECK(mm_chunk_put(&f.edit, 10, 5, 3) == MM_CHUNK_OK && f.outputs == 1);
    init(&f);
    f.edit.output = NULL;
    CHECK(mm_chunk_add(&f.edit, 10, 0, 0, 0, NULL) == MM_CHUNK_OK && !f.outputs);
}
static void simple(void)
{
    Fixture f;
    MMU32 position, number;
    int remove;
    init(&f);
    node(&f, 0, 0, 0, 0);
    node(&f, 1, 1, 0, 0);
    node(&f, 2, 3, 0, 0);
    CHECK(mm_chunk_unique(&f.edit, 10, &position, &number) == MM_CHUNK_OK && position == 2 && number == 2);
    CHECK(mm_chunk_unique(&f.edit, 9, &position, &number) == MM_CHUNK_OK && !position && !number);
    CHECK(mm_chunk_unique(&f.edit, 11, &position, &number) == MM_CHUNK_OK && position == 3 && !number);
    f.nodes[0].entry.parent_count = 1;
    CHECK(mm_chunk_decrement(&f.edit, 0, &remove) == MM_CHUNK_OK && remove);
    remove = 123;
    CHECK(mm_chunk_decrement(&f.edit, 0, &remove) == MM_CHUNK_BAD_FILE && remove == 123);
    f.nodes[0].entry.parent_count = 1;
    f.nodes[0].entry.flags = 2;
    CHECK(mm_chunk_decrement(&f.edit, 0, &remove) == MM_CHUNK_OK && !remove);
    f.logs = 0;
    memset(f.log, 0, sizeof(f.log));
    CHECK(mm_chunk_remove(&f.edit, 0) == MM_CHUNK_OK && !strcmp(f.log, "FRE") && f.count == 2);
    CHECK(mm_chunk_remove(&f.edit, 2) == MM_CHUNK_BAD_FILE);
    CHECK(mm_chunk_put(NULL, 0, 0, 0) == MM_CHUNK_BAD_FILE);
    CHECK(mm_chunk_add(&f.edit, 10, 5, 10, 3, NULL) == MM_CHUNK_BAD_FILE);
}
static void link_fixture_node(Fixture *f, MMU32 position, MMU32 target)
{
    Node *parent = f->nodes + position;
    MMU32 slot = parent->entry.child_count++;
    CHECK(slot < 4);
    parent->children[slot].type = 10;
    parent->children[slot].number = target;
    parent->children[slot].slot = slot;
}
static void deletes(void)
{
    Fixture f;
    int loner;
    for (loner = 0; loner < 2; ++loner)
    {
        init(&f);
        node(&f, 0, 0, 2, 0);
        node(&f, 1, 1, 0, 1);
        node(&f, 2, 2, 0, 1);
        node(&f, 3, 3, loner ? 2 : 0, 2);
        link_fixture_node(&f, 0, 1);
        link_fixture_node(&f, 0, 2);
        link_fixture_node(&f, 1, 3);
        link_fixture_node(&f, 2, 3);
        CHECK(mm_chunk_delete(&f.edit, 10, 0) == MM_CHUNK_OK && f.count == (MMU32)loner);
        if (loner)
            CHECK(f.nodes[0].entry.number == 3 && !f.nodes[0].entry.parent_count && f.nodes[0].entry.flags == 2);
        CHECK(f.frees == (loner ? 3 : 4) && !f.issues[0] && !f.issues[1] &&
              f.walk_allocations && !f.live_walk_buffers);
    }
    init(&f);
    node(&f, 0, 0, 2, 1);
    CHECK(mm_chunk_delete(&f.edit, 10, 0) == MM_CHUNK_OK && !f.walk_allocations && !f.nodes[0].entry.flags);
    CHECK(mm_chunk_delete(&f.edit, 10, 0) == MM_CHUNK_BAD_FILE);
    CHECK(mm_chunk_delete(&f.edit, 10, 99) == MM_CHUNK_NOT_FOUND);
    /* Failed traversal allocation: drop duplicate child references in reverse
     * order, but do not recursively delete newly unreferenced descendants. */
    init(&f);
    node(&f, 0, 0, 2, 0);
    node(&f, 1, 1, 0, 1);
    node(&f, 2, 2, 0, 2);
    node(&f, 3, 3, 0, 1);
    link_fixture_node(&f, 0, 1);
    link_fixture_node(&f, 1, 2);
    link_fixture_node(&f, 1, 2);
    link_fixture_node(&f, 1, 3);
    f.fail = 5;
    CHECK(mm_chunk_delete(&f.edit, 10, 0) == MM_CHUNK_OK && f.count == 2);
    CHECK(f.nodes[0].entry.number == 2 && !f.nodes[0].entry.parent_count && !f.nodes[0].entry.flags);
    CHECK(f.nodes[1].entry.number == 3 && !f.nodes[1].entry.parent_count && !f.nodes[1].entry.flags);
    CHECK(f.stores == 5 && f.stored_numbers[2] == 3 && f.stored_numbers[3] == 2 && f.stored_numbers[4] == 2);
    CHECK(f.issues[MM_EDIT_LOW_MEMORY] == 1 && f.frees == 2 && f.walk_allocations == 1 && !f.live_walk_buffers);
}
static void links(void)
{
    Fixture f;
    MMChunkChild key;
    MMChunkAccess access;
    MMU32 number = 99;
    Node parent, target;
    init(&f);
    node(&f, 0, 0, 2, 0);
    node(&f, 1, 1, 22, 0);
    f.nodes[0].entry.variable_bytes = 16;
    key.type = 10;
    key.number = 1;
    key.slot = 9;
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 0) == MM_CHUNK_OK);
    CHECK(f.nodes[0].entry.child_count == 1 && f.nodes[0].entry.variable_bytes == 28 &&
          f.nodes[1].entry.parent_count == 1 && f.nodes[1].entry.flags == 22);
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 1) == MM_CHUNK_OK && f.nodes[1].entry.flags == 22);
    parent = f.nodes[0];
    target = f.nodes[1];
    f.unreadable_child = 1;
    access = mm_chunk_edit_access(&f.edit);
    CHECK(mm_chunk_link_number(&access, 10, 0, 9, 10, &number) == MM_CHUNK_IO && number == 99);
    CHECK(mm_chunk_unlink(&f.edit, 10, 0, &key) == MM_CHUNK_IO);
    key.slot = 1;
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 1) == MM_CHUNK_IO);
    f.unreadable_child = 0;
    f.unreadable_entry = 1;
    CHECK(mm_chunk_put(&f.edit, 10, 1, 10) == MM_CHUNK_IO);
    f.unreadable_entry = 0;
    CHECK(!memcmp(&parent, f.nodes, sizeof(parent)) && !memcmp(&target, f.nodes + 1, sizeof(target)));
    f.edit.max_references = 1;
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 1) == MM_CHUNK_BAD_FILE);
    CHECK(!memcmp(&parent, f.nodes, sizeof(parent)) && !memcmp(&target, f.nodes + 1, sizeof(target)));
    f.edit.max_references = 3;
    f.fail = 4;
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 1) == MM_CHUNK_MEMORY &&
          !memcmp(&parent, f.nodes, sizeof(parent)));
    f.fail = 0;
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 1) == MM_CHUNK_OK);
    CHECK(f.nodes[0].entry.child_count == 2 && f.nodes[0].entry.variable_bytes == 40 &&
          f.nodes[1].entry.parent_count == 2 && f.nodes[1].entry.flags == 20);
    key.slot = 9;
    CHECK(mm_chunk_change_slot(&f.edit, 10, 0, &key, 0) == MM_CHUNK_OK &&
          f.nodes[0].children[0].slot == 0 && f.nodes[0].children[1].slot == 1);
    key.slot = 0;
    CHECK(mm_chunk_change_slot(&f.edit, 10, 0, &key, 10) == MM_CHUNK_OK &&
          f.nodes[0].children[0].slot == 1 && f.nodes[0].children[1].slot == 10);
    key.slot = 10;
    parent = f.nodes[0];
    CHECK(mm_chunk_change_slot(&f.edit, 10, 0, &key, 1) == MM_CHUNK_BAD_FILE &&
          !memcmp(&parent, f.nodes, sizeof(parent)));
    CHECK(mm_chunk_change_slot(&f.edit, 99, 99, &key, 10) == MM_CHUNK_OK);
    key.slot = 1;
    CHECK(mm_chunk_unlink(&f.edit, 10, 0, &key) == MM_CHUNK_OK && f.nodes[1].entry.parent_count == 1);
    key.slot = 10;
    CHECK(mm_chunk_unlink(&f.edit, 10, 0, &key) == MM_CHUNK_OK && f.count == 1 && f.frees == 1);
    CHECK(!f.nodes[0].entry.child_count && f.nodes[0].entry.variable_bytes == 16 &&
          !strcmp(f.nodes[0].name, "retained"));
    init(&f);
    node(&f, 0, 0, 2, 0);
    node(&f, 1, 1, 2, 1);
    f.nodes[0].entry.variable_bytes = 16;
    parent = f.nodes[0];
    target = f.nodes[1];
    key.slot = 5;
    f.edit.max_references = 1;
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 1) == MM_CHUNK_BAD_FILE); /* Child-reference overflow. */
    CHECK(!memcmp(&parent, f.nodes, sizeof(parent)) && !memcmp(&target, f.nodes + 1, sizeof(target)));
    f.edit.max_references = 3;
    node(&f, 2, 2, 0, 1);
    f.nodes[0].entry.parent_count = 1;
    link_fixture_node(&f, 1, 2);
    link_fixture_node(&f, 2, 0);
    parent = f.nodes[0];
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 1) == MM_CHUNK_BAD_FILE);
    CHECK(f.issues[MM_EDIT_CYCLE_RISK] == 1 && !memcmp(&parent, f.nodes, sizeof(parent)) &&
          f.walk_allocations == 1 && !f.live_walk_buffers);
    f.fail = 5;
    CHECK(mm_chunk_adopt(&f.edit, 10, 0, &key, 1) == MM_CHUNK_BAD_FILE);
    CHECK(f.issues[MM_EDIT_CYCLE_RISK] == 2 && !memcmp(&parent, f.nodes, sizeof(parent)) &&
          f.walk_allocations == 2 && !f.live_walk_buffers);
    CHECK(mm_chunk_unlink(&f.edit, 10, 0, &key) == MM_CHUNK_NOT_FOUND);
    CHECK(mm_chunk_adopt(&f.edit, 10, 99, &key, 1) == MM_CHUNK_NOT_FOUND);
}
static void flag_cases(void)
{
    Fixture f;
    Node original;
    MMU32 flag;
    for (flag = MM_CHUNK_PACKED; flag <= MM_CHUNK_FOREST; flag *= 4)
    {
        init(&f);
        node(&f, 0, 5, 2, 1);
        original = f.nodes[0];
        CHECK(mm_chunk_set_flag(&f.edit, 10, 5, flag, 0) == MM_CHUNK_OK && !f.resets && !f.stores);
        CHECK(mm_chunk_set_flag(&f.edit, 10, 5, flag, 1) == MM_CHUNK_OK && f.resets == 1 && f.stores == 1 &&
              !f.nodes[0].runtime);
        original.entry.flags |= flag;
        original.runtime = 0;
        CHECK(!memcmp(&f.nodes[0], &original, sizeof(original)));
        CHECK(mm_chunk_set_flag(&f.edit, 10, 5, flag, 1) == MM_CHUNK_OK && f.resets == 1 && f.stores == 1);
        CHECK(mm_chunk_set_flag(&f.edit, 10, 5, flag, 0) == MM_CHUNK_OK && f.resets == 2 && f.stores == 2 &&
              f.nodes[0].entry.flags == 2);
    }
    init(&f);
    node(&f, 0, 5, 0, 1);
    CHECK(mm_chunk_set_flag(&f.edit, 10, 5, 2, 1) == MM_CHUNK_OK && f.nodes[0].entry.flags == 2 &&
          !f.resets && f.nodes[0].runtime == 99);
    CHECK(mm_chunk_set_flag(&f.edit, 10, 5, 2, 0) == MM_CHUNK_OK && !f.nodes[0].entry.flags && !f.walk_allocations &&
          f.count == 1 && !f.resets);
    CHECK(mm_chunk_set_flag(&f.edit, 10, 5, 2, 0) == MM_CHUNK_OK && f.count == 1 && !f.walk_allocations);
    CHECK(mm_chunk_set_flag(&f.edit, 99, 5, 4, 1) == MM_CHUNK_NOT_FOUND);
    CHECK(mm_chunk_set_flag(&f.edit, 10, 5, 1, 1) == MM_CHUNK_BAD_FILE);
    init(&f);
    node(&f, 0, 5, 2, 0);
    CHECK(mm_chunk_set_flag(&f.edit, 10, 5, 2, 0) == MM_CHUNK_OK && !f.count && f.frees == 1);
}
int main(void)
{
    flag_cases();
    put_cases();
    simple();
    deletes();
    links();
    return 0;
}
