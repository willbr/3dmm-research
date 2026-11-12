#include "three_mm/chunk_edit.h"
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
    ThreeMMChunkEntry entry;
    int runtime;
    char name[16];
    ThreeMMChunkChild children[4];
} Node;
typedef struct Fixture
{
    Node nodes[32];
    ThreeMMU32 count;
    ThreeMMChunkEdit edit;
    ThreeMMChunkLocation output;
    ThreeMMChunkLocation freed[32];
    int frees, resets, clears, outputs, extra, fail;
    int file, output_added, next, total, begun, issues[4];
    ThreeMMChunkDeleteEvent events[16];
    int skips[16];
    char log[256];
    int logs;
    ThreeMMU32 stored_numbers[32];
    int stores;
} Fixture;
static void log_event(Fixture *f, char ch)
{
    CHECK(f->logs < 255);
    f->log[f->logs++] = ch;
}
static ThreeMMU32 count(void *ctx)
{
    return ((Fixture *)ctx)->count;
}
static void entry(void *ctx, ThreeMMU32 i, ThreeMMChunkEntry *entry)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(i < f->count);
    *entry = f->nodes[i].entry;
}
static void child(void *ctx, ThreeMMU32 p, ThreeMMU32 i, ThreeMMChunkChild *out)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(p < f->count && i < f->nodes[p].entry.child_count && i < 4);
    *out = f->nodes[p].children[i];
}
static int insert(void *ctx, ThreeMMU32 i)
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
static void store(void *ctx, ThreeMMU32 i, const ThreeMMChunkEntry *record, int initialize)
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
static void erase(void *ctx, ThreeMMU32 i)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(i < f->count);
    log_event(f, 'E');
    --f->count;
    memmove(f->nodes + i, f->nodes + i + 1, (f->count - i) * sizeof(Node));
}
static int allocate(void *ctx, ThreeMMU32 bytes, ThreeMMChunkLocation *out)
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
static void free_space(void *ctx, int extra, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    Fixture *f = (Fixture *)ctx;
    ThreeMMChunkLocation *out;
    log_event(f, 'F');
    CHECK(f->frees < 32);
    out = f->freed + f->frees++;
    memset(out, 0, sizeof(*out));
    out->offset = offset;
    out->bytes = bytes;
    out->flags = (ThreeMMU32)extra;
}
static int write(void *ctx, const ThreeMMChunkLocation *where, int added)
{
    Fixture *f = (Fixture *)ctx;
    log_event(f, 'W');
    CHECK(f->outputs == 1 && f->output.offset == where->offset && f->output_added == added);
    return f->fail != 3;
}
static void output(void *ctx, const ThreeMMChunkLocation *where, int added)
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
static void reset(void *ctx, ThreeMMU32 type, ThreeMMU32 number)
{
    Fixture *f = (Fixture *)ctx;
    ThreeMMU32 i;
    log_event(f, 'R');
    ++f->resets;
    for (i = 0; i < f->count; ++i)
        if (f->nodes[i].entry.type == type && f->nodes[i].entry.number == number)
            f->nodes[i].runtime = 0;
}
static void begin(void *ctx, ThreeMMU32 type, ThreeMMU32 number)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(type == 10 && number == 0 && !f->begun);
    ++f->begun;
}
static int next(void *ctx, int skip, ThreeMMChunkDeleteEvent *out)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(f->begun == 1);
    if (f->next == f->total)
        return 0;
    CHECK(skip == f->skips[f->next]);
    *out = f->events[f->next++];
    return 1;
}
static void issue(void *ctx, int kind)
{
    Fixture *f = (Fixture *)ctx;
    CHECK(kind >= 0 && kind < 4);
    ++f->issues[kind];
}
static void init(Fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->output.offset = 999;
    f->edit.context = f;
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
    f->edit.begin_delete = begin;
    f->edit.next_delete = next;
    f->edit.issue = issue;
}
static void node(Fixture *f, unsigned i, ThreeMMU32 number, ThreeMMU32 flags, ThreeMMU32 parents)
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
                    result = three_mm_chunk_put(&f.edit, 10, 5, zero ? 0 : 9);
                    CHECK(!strcmp(f.log, !fails      ? (existing ? "AOWFSR" : "IASOW")
                                         : existing  ? (fail == 2 ? "A" : "AOWFC")
                                         : fail == 1 ? "IC"
                                         : fail == 2 ? "IAEC"
                                                     : "IASOWSFREC"));
                    CHECK(result == (!fails      ? THREE_MM_CHUNK_OK
                                     : fail == 1 ? THREE_MM_CHUNK_MEMORY
                                                 : THREE_MM_CHUNK_IO));
                    if (fails)
                    {
                        CHECK(f.count == (ThreeMMU32)existing);
                        if (existing)
                            CHECK(!memcmp(f.nodes, &old, sizeof(old)));
                        CHECK(f.clears == (!existing || fail == 3));
                        CHECK(f.output.offset == (existing && fail == 2 ? 999U : 0U));
                        CHECK(f.frees == (fail == 3) && f.resets == (!existing && fail == 3));
                        if (fail == 3)
                            CHECK(f.freed[0].offset == (zero ? 0U : 200U) &&
                                  f.freed[0].flags == (ThreeMMU32)(extra && !zero));
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
    CHECK(three_mm_chunk_put(&f.edit, 10, 5, 3) == THREE_MM_CHUNK_OK && f.outputs == 1);
    init(&f);
    f.edit.output = NULL;
    CHECK(three_mm_chunk_add(&f.edit, 10, 0, 0, 0, NULL) == THREE_MM_CHUNK_OK && !f.outputs);
}
static void simple(void)
{
    Fixture f;
    ThreeMMU32 position, number;
    int remove;
    init(&f);
    node(&f, 0, 0, 0, 0);
    node(&f, 1, 1, 0, 0);
    node(&f, 2, 3, 0, 0);
    CHECK(three_mm_chunk_unique(&f.edit, 10, &position, &number) == THREE_MM_CHUNK_OK && position == 2 && number == 2);
    CHECK(three_mm_chunk_unique(&f.edit, 9, &position, &number) == THREE_MM_CHUNK_OK && !position && !number);
    CHECK(three_mm_chunk_unique(&f.edit, 11, &position, &number) == THREE_MM_CHUNK_OK && position == 3 && !number);
    f.nodes[0].entry.parent_count = 1;
    CHECK(three_mm_chunk_decrement(&f.edit, 0, &remove) == THREE_MM_CHUNK_OK && remove);
    remove = 123;
    CHECK(three_mm_chunk_decrement(&f.edit, 0, &remove) == THREE_MM_CHUNK_BAD_FILE && remove == 123);
    f.nodes[0].entry.parent_count = 1;
    f.nodes[0].entry.flags = 2;
    CHECK(three_mm_chunk_decrement(&f.edit, 0, &remove) == THREE_MM_CHUNK_OK && !remove);
    f.logs = 0;
    memset(f.log, 0, sizeof(f.log));
    CHECK(three_mm_chunk_remove(&f.edit, 0) == THREE_MM_CHUNK_OK && !strcmp(f.log, "FRE") && f.count == 2);
    CHECK(three_mm_chunk_remove(&f.edit, 2) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_put(NULL, 0, 0, 0) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_add(&f.edit, 10, 5, 10, 3, NULL) == THREE_MM_CHUNK_BAD_FILE);
}
static void event(Fixture *f, unsigned i, unsigned number, unsigned flags, int skip)
{
    f->events[i].type = 10;
    f->events[i].number = number;
    f->events[i].flags = flags;
    f->skips[i] = skip;
    f->total = (int)i + 1;
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
        event(&f, 0, 0, 5, 0);
        event(&f, 1, 1, 1, 0);
        event(&f, 2, 3, 3, 0);
        event(&f, 3, 1, 2, 1);
        event(&f, 4, 2, 1, 1);
        event(&f, 5, 3, 3, 0);
        event(&f, 6, 2, 2, !loner ? 0 : 1);
        event(&f, 7, 0, 6, 1);
        CHECK(three_mm_chunk_delete(&f.edit, 10, 0) == THREE_MM_CHUNK_OK && f.count == (ThreeMMU32)loner);
        if (loner)
            CHECK(f.nodes[0].entry.number == 3 && !f.nodes[0].entry.parent_count && f.nodes[0].entry.flags == 2);
        CHECK(f.frees == (loner ? 3 : 4) && !f.issues[0] && !f.issues[1]);
    }
    init(&f);
    node(&f, 0, 0, 2, 1);
    CHECK(three_mm_chunk_delete(&f.edit, 10, 0) == THREE_MM_CHUNK_OK && !f.begun && !f.nodes[0].entry.flags);
    CHECK(three_mm_chunk_delete(&f.edit, 10, 0) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_delete(&f.edit, 10, 99) == THREE_MM_CHUNK_NOT_FOUND);
    /* Failed traversal allocation: drop duplicate child references in reverse
     * order, but do not recursively delete newly unreferenced descendants. */
    init(&f);
    node(&f, 0, 0, 2, 0);
    node(&f, 1, 1, 0, 1);
    node(&f, 2, 2, 0, 2);
    node(&f, 3, 3, 0, 1);
    f.nodes[1].entry.child_count = 3;
    f.nodes[1].children[0].type = f.nodes[1].children[1].type = f.nodes[1].children[2].type = 10;
    f.nodes[1].children[0].number = f.nodes[1].children[1].number = 2;
    f.nodes[1].children[2].number = 3;
    event(&f, 0, 0, 5, 0);
    event(&f, 1, 1, 11, 0);
    event(&f, 2, 0, 6, 0);
    CHECK(three_mm_chunk_delete(&f.edit, 10, 0) == THREE_MM_CHUNK_OK && f.count == 2);
    CHECK(f.nodes[0].entry.number == 2 && !f.nodes[0].entry.parent_count && !f.nodes[0].entry.flags);
    CHECK(f.nodes[1].entry.number == 3 && !f.nodes[1].entry.parent_count && !f.nodes[1].entry.flags);
    CHECK(f.stores == 5 && f.stored_numbers[2] == 3 && f.stored_numbers[3] == 2 && f.stored_numbers[4] == 2);
    CHECK(f.issues[THREE_MM_EDIT_LOW_MEMORY] == 1 && f.frees == 2);
}
int main(void)
{
    put_cases();
    simple();
    deletes();
    return 0;
}
