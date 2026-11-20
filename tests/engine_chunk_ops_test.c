#include "mm/chunk_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{
    if (!value)
    {
        fprintf(stderr, "C89 chunk ops failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Node
{
    MMChunkEntry e;
    MMChunkChild kids[16];
    MMU32 raw_count;
    MMI32 runtime;
    char name[24];
} Node;
typedef struct Host
{
    Node nodes[64];
    MMChunkEntry payload_entry;
    MMU32 count, end;
    unsigned char physical[8192], payload[64];
    const unsigned char *write_data;
    MMChunkEndpoint ops;
    MMChunkWalk deletion;
    MMStorageAllocator memory;
    int live, calls, fail_map, fail_growth, fail_walk, fail_payload, fail_name, fail_link, fail_runtime;
    int payloads, names, links, resets, reads, fail_read;
} Host;
static MMU32 find(Host *h, MMU32 type, MMU32 number)
{
    MMU32 i;
    for (i = 0; i < h->count; ++i)
        if (h->nodes[i].e.type == type && h->nodes[i].e.number == number)
            return i;
    return h->count;
}
static void size(Node *n)
{
    n->e.variable_bytes = n->raw_count * 12 + (n->name[0] ? (MMU32)strlen(n->name) + 1 : 0);
}
static MMU32 count(void *c)
{
    return ((Host *)c)->count;
}
static void entry(void *c, MMU32 i, MMChunkEntry *out)
{
    Host *h = (Host *)c;
    CHECK(i < h->count);
    *out = h->nodes[i].e;
}
static void child(void *c, MMU32 p, MMU32 i, MMChunkChild *out)
{
    Host *h = (Host *)c;
    CHECK(p < h->count && i < h->nodes[p].e.child_count);
    *out = h->nodes[p].kids[i];
}
static void store(void *c, MMU32 i, const MMChunkEntry *e, int initialize)
{
    Host *h = (Host *)c;
    MMU32 bytes;
    CHECK(i < h->count);
    bytes = h->nodes[i].e.variable_bytes;
    if (initialize)
        memset(h->nodes + i, 0, sizeof(Node));
    h->nodes[i].e = *e;
    h->nodes[i].e.variable_bytes = bytes;
}
static int insert(void *c, MMU32 i)
{
    Host *h = (Host *)c;
    CHECK(i <= h->count && h->count < 64);
    memmove(h->nodes + i + 1, h->nodes + i, (h->count - i) * sizeof(Node));
    memset(h->nodes + i, 0, sizeof(Node));
    ++h->count;
    return 1;
}
static void erase(void *c, MMU32 i)
{
    Host *h = (Host *)c;
    CHECK(i < h->count);
    --h->count;
    memmove(h->nodes + i, h->nodes + i + 1, (h->count - i) * sizeof(Node));
}
static int allocate(void *c, MMU32 bytes, MMChunkLocation *out)
{
    Host *h = (Host *)c;
    CHECK(h->end + bytes <= sizeof(h->physical));
    out->file = h;
    out->offset = bytes ? h->end : 0;
    out->bytes = bytes;
    out->flags = 0;
    h->end += bytes;
    return 1;
}
static void free_space(void *c, int extra, MMU32 offset, MMU32 bytes)
{
    (void)c;
    CHECK(!extra && offset + bytes <= 8192);
}
static int write(void *c, const MMChunkLocation *location, int added)
{
    Host *h = (Host *)c;
    (void)added;
    ++h->payloads;
    if (h->fail_payload == h->payloads)
        return 0;
    memcpy(h->physical + location->offset, h->write_data, location->bytes);
    return 1;
}
static MMI32 runtime(void *c, MMU32 type, MMU32 number)
{
    Host *h = (Host *)c;
    MMU32 i = find(h, type, number);
    return i < h->count ? h->nodes[i].runtime : 0;
}
static int set_runtime(void *c, MMU32 type, MMU32 number, MMI32 value)
{
    Host *h = (Host *)c;
    MMU32 i = find(h, type, number);
    CHECK(i < h->count);
    if (value && h->fail_runtime)
        return 0;
    h->nodes[i].runtime = value;
    return 1;
}
static void reset(void *c, MMU32 type, MMU32 number)
{
    Host *h = (Host *)c;
    ++h->resets;
    CHECK(set_runtime(c, type, number, 0));
}
static MMU32 children(void *c, MMU32 type, MMU32 number)
{
    Host *h = (Host *)c;
    MMU32 i = find(h, type, number);
    CHECK(i < h->count);
    return h->nodes[i].e.child_count;
}
static int graph_child(void *c, MMU32 type, MMU32 number, MMU32 k, MMChunkChild *out)
{
    Host *h = (Host *)c;
    MMU32 i = find(h, type, number);
    CHECK(i < h->count);
    if (k >= h->nodes[i].e.child_count)
        return 0;
    *out = h->nodes[i].kids[k];
    return 1;
}
static void begin(void *c, MMU32 type, MMU32 number)
{
    Host *h = (Host *)c;
    CHECK(mm_chunk_walk_reset(&h->deletion, &h->ops.graph, type, number) == MM_CHUNK_OK);
}
static int next(void *c, int skip, MMChunkDeleteEvent *out)
{
    Host *h = (Host *)c;
    MMChunkChild node;
    MMU32 flags;
    if (!mm_chunk_walk_next(&h->deletion, skip, &node, NULL, &flags))
        return 0;
    out->type = node.type;
    out->number = node.number;
    out->flags = (flags & MM_WALK_PRE ? 1U : 0U) | (flags & MM_WALK_POST ? 2U : 0U) |
                 (flags & MM_WALK_ROOT ? 4U : 0U) | (flags & MM_WALK_ERROR ? 8U : 0U);
    return 1;
}
static int insert_child(void *c, MMU32 p, MMU32 i, const MMChunkChild *kid)
{
    Host *h = (Host *)c;
    Node *n = h->nodes + p;
    CHECK(i <= n->raw_count && n->raw_count < 16);
    ++h->links;
    if (h->fail_link == h->links)
        return 0;
    memmove(n->kids + i + 1, n->kids + i, (n->raw_count - i) * sizeof(*kid));
    n->kids[i] = *kid;
    ++n->raw_count;
    size(n);
    return 1;
}
static void erase_child(void *c, MMU32 p, MMU32 i)
{
    Node *n = ((Host *)c)->nodes + p;
    CHECK(i < n->raw_count);
    --n->raw_count;
    memmove(n->kids + i, n->kids + i + 1, (n->raw_count - i) * sizeof(*n->kids));
    size(n);
}
static void put_child(void *c, MMU32 p, MMU32 i, const MMChunkChild *kid)
{
    Host *h = (Host *)c;
    CHECK(p < h->count && i < h->nodes[p].raw_count);
    h->nodes[p].kids[i] = *kid;
}
static int descendant(void *c, MMU32 type, MMU32 number, MMU32 target_type, MMU32 target_number)
{
    Host *h = (Host *)c;
    MMU32 i = find(h, target_type, target_number);
    CHECK(i < h->count);
    return mm_chunk_reaches(&h->ops.graph, NULL, type, number, target_type, target_number,
                                  h->nodes[i].e.parent_count);
}
static void move_entry(void *c, MMU32 from, MMU32 to)
{
    Host *h = (Host *)c;
    Node saved = h->nodes[from];
    CHECK(from < h->count && to <= h->count);
    if (to > from)
        --to;
    if (from < to)
        memmove(h->nodes + from, h->nodes + from + 1, (to - from) * sizeof(Node));
    else if (from > to)
        memmove(h->nodes + to + 1, h->nodes + to, (from - to) * sizeof(Node));
    h->nodes[to] = saved;
}
static int move_bytes(void *c, MMU32 from, MMU32 offset, MMU32 to, MMU32 target, MMU32 bytes)
{
    Host *h = (Host *)c;
    MMChunkChild saved[16];
    Node *a = h->nodes + from, *b = h->nodes + to;
    CHECK(from != to && offset % 12 == 0 && target % 12 == 0 && bytes % 12 == 0);
    offset /= 12;
    target /= 12;
    bytes /= 12;
    CHECK(offset + bytes <= a->raw_count && target <= b->raw_count && b->raw_count + bytes <= 16);
    memcpy(saved, a->kids + offset, bytes * sizeof(*saved));
    a->raw_count -= bytes;
    memmove(a->kids + offset, a->kids + offset + bytes, (a->raw_count - offset) * sizeof(*saved));
    memmove(b->kids + target + bytes, b->kids + target, (b->raw_count - target) * sizeof(*saved));
    memcpy(b->kids + target, saved, bytes * sizeof(*saved));
    b->raw_count += bytes;
    size(a);
    size(b);
    return 1;
}
static void *read_payload(void *c, MMU32 i, const MMChunkEntry *e)
{
    Host *h = (Host *)c;
    CHECK(i < h->count && e->number == h->nodes[i].e.number && e->bytes <= sizeof(h->payload));
    if (++h->reads == h->fail_read)
        return NULL;
    h->payload_entry = *e;
    memcpy(h->payload, h->physical + e->offset, e->bytes);
    return h;
}
static int put_payload(void *c, MMU32 type, MMU32 number, void *source)
{
    Host *h = (Host *)c, *src = (Host *)source;
    MMU32 i;
    int result;
    h->write_data = src->payload;
    result = mm_chunk_put(&h->ops.edit, type, number, src->payload_entry.bytes);
    if (result != MM_CHUNK_OK)
        return 0;
    i = find(h, type, number);
    h->nodes[i].e.flags = (h->nodes[i].e.flags & ~4U) | (src->payload_entry.flags & 4U);
    return 1;
}
static int add_payload(void *c, MMU32 type, void *source, MMU32 *number)
{
    Host *h = (Host *)c;
    MMU32 position, n;
    CHECK(mm_chunk_unique(&h->ops.edit, type, &position, &n) == MM_CHUNK_OK);
    if (!put_payload(c, type, n, source))
        return 0;
    *number = n;
    return 1;
}
static const void *read_name(void *c, MMU32 i)
{
    Host *h = (Host *)c;
    CHECK(i < h->count);
    return h->nodes[i].name[0] ? h->nodes[i].name : NULL;
}
static int write_name(void *c, MMU32 i, const void *name)
{
    Host *h = (Host *)c;
    CHECK(i < h->count);
    ++h->names;
    if (h->fail_name == h->names)
        return 0;
    memcpy(h->nodes[i].name, name, sizeof(h->nodes[i].name));
    size(h->nodes + i);
    return 1;
}
static void *resize(void *c, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = (Host *)c;
    void *fresh;
    (void)priority;
    ++h->calls;
    if ((h->fail_map && (bytes == 128 || bytes == 12)) || (h->fail_growth && old_bytes == 128) ||
        (h->fail_walk && (bytes == 288 || bytes == 160)))
        return NULL;
    fresh = malloc(bytes);
    CHECK(fresh != NULL);
    if (old)
    {
        memcpy(fresh, old, bytes < old_bytes ? bytes : old_bytes);
        free(old);
    }
    else
        ++h->live;
    return fresh;
}
static void release(void *c, void *data, MMU32 bytes)
{
    Host *h = (Host *)c;
    (void)bytes;
    CHECK(h->live > 0);
    --h->live;
    free(data);
}
static void init(Host *h)
{
    MMChunkEdit *e;
    memset(h, 0, sizeof(*h));
    h->end = 128;
    h->memory.context = h;
    h->memory.resize = resize;
    h->memory.release = release;
    h->ops.context = h;
    h->ops.identity = h;
    h->ops.allocator = &h->memory;
    h->ops.max_references = 65535;
    e = &h->ops.edit;
    e->context = h;
    e->count = count;
    e->entry = entry;
    e->child = child;
    e->store = store;
    e->insert = insert;
    e->erase = erase;
    e->allocate = allocate;
    e->free_space = free_space;
    e->write = write;
    e->reset_runtime = reset;
    e->begin_delete = begin;
    e->next_delete = next;
    e->insert_child = insert_child;
    e->erase_child = erase_child;
    e->put_child = put_child;
    e->descendant = descendant;
    e->move_entry = move_entry;
    e->move_bytes = move_bytes;
    h->ops.graph.context = h;
    h->ops.graph.children = children;
    h->ops.graph.child = graph_child;
    h->ops.runtime = runtime;
    h->ops.set_runtime = set_runtime;
    h->ops.read_payload = read_payload;
    h->ops.put_payload = put_payload;
    h->ops.add_payload = add_payload;
    h->ops.read_name = read_name;
    h->ops.write_name = write_name;
    CHECK(mm_chunk_walk_init(&h->deletion, NULL) == MM_CHUNK_OK);
}
static void dispose(Host *h)
{
    CHECK(!h->live);
    CHECK(mm_chunk_walk_dispose(&h->deletion) == MM_CHUNK_OK);
}
static void add(Host *h, MMU32 number)
{
    MMChunkLocation location;
    MMU32 position;
    static const unsigned char bytes[3] = {1, 2, 3};
    CHECK(mm_chunk_unique(&h->ops.edit, 10, &position, &location.flags) == MM_CHUNK_OK);
    CHECK(mm_chunk_add(&h->ops.edit, 10, number, position, 3, &location) == MM_CHUNK_OK);
    memcpy(h->physical + location.offset, bytes, 3);
    memcpy(h->nodes[position].name, "named", 6);
    size(h->nodes + position);
}
static void link(Host *h, MMU32 parent, MMU32 target, MMU32 slot)
{
    MMChunkChild kid;
    kid.type = 10;
    kid.number = target;
    kid.slot = slot;
    CHECK(mm_chunk_adopt(&h->ops.edit, 10, parent, &kid, 1, 65535) == MM_CHUNK_OK);
}
static void graph(Host *h)
{
    add(h, 0);
    add(h, 1);
    add(h, 2);
    link(h, 0, 2, 2);
    link(h, 0, 1, 3);
    link(h, 2, 1, 3);
    h->nodes[0].e.flags |= 16;
    h->nodes[1].e.flags |= 4;
}
static void copies(void)
{
    Host src, dst;
    MMU32 first, copied, cloned, branch, direct, i;
    int failure;
    init(&src);
    init(&dst);
    graph(&src);
    add(&dst, 0);
    CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 0, &first) == MM_CHUNK_OK && dst.count == 4);
    CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 0, &copied) == MM_CHUNK_OK && first == copied &&
          dst.count == 4);
    CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 1, &cloned) == MM_CHUNK_OK && cloned != first &&
          dst.count == 7);
    i = find(&dst, 10, cloned);
    branch = dst.nodes[i].kids[0].number;
    direct = dst.nodes[i].kids[1].number;
    CHECK(dst.nodes[find(&dst, 10, branch)].kids[0].number == direct && (dst.nodes[i].e.flags & 18U) == 18U);
    CHECK((dst.nodes[find(&dst, 10, direct)].e.flags & 4U) == 4U &&
          dst.nodes[find(&dst, 10, direct)].e.parent_count == 2);
    src.nodes[1].runtime = 0;
    copied = 999;
    CHECK(!mm_chunk_match(&src.ops, &dst.ops, 10, 0, &copied) && copied == 999);
    CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 0, &copied) == MM_CHUNK_OK && dst.count == 10 &&
          copied != first && copied != cloned);
    first = copied;
    CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 0, &copied) == MM_CHUNK_OK && copied == first &&
          dst.count == 10);
    CHECK(mm_chunk_copy(&src.ops, &src.ops, 10, 1, 0, &copied) == MM_CHUNK_OK && copied == 1 &&
          src.count == 3 && (src.nodes[1].e.flags & 2));
    CHECK(mm_chunk_copy(&src.ops, &src.ops, 10, 0, 1, &copied) == MM_CHUNK_OK && copied != 0 &&
          src.count == 6);
    dispose(&src);
    dispose(&dst);
    init(&src);
    init(&dst);
    add(&src, 0);
    add(&dst, 0xffffffffUL);
    src.nodes[0].runtime = dst.nodes[0].runtime = 77;
    CHECK(mm_chunk_match(&src.ops, &dst.ops, 10, 0, &copied) && copied == 0xffffffffUL);
    dst.nodes[0].e.child_count = 1;
    copied = 999;
    CHECK(!mm_chunk_match(&src.ops, &dst.ops, 10, 0, &copied) &&
          copied == 999); /* Final candidate must not wrap. */
    dispose(&src);
    dispose(&dst);
    init(&src);
    init(&dst);
    graph(&src);
    src.fail_runtime = 1;
    first = (MMU32)mm_chunk_runtime_highest();
    CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 0, &copied) == MM_CHUNK_OK);
    CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 0, &copied) == MM_CHUNK_OK && dst.count == 6);
    CHECK((MMU32)mm_chunk_runtime_highest() == first && !src.nodes[0].runtime);
    dispose(&src);
    dispose(&dst);
    for (failure = 1; failure <= 6; ++failure)
    {
        init(&src);
        init(&dst);
        graph(&src);
        copied = 999;
        if (failure == 1)
            dst.fail_payload = 1;
        if (failure == 2)
            dst.fail_name = 1;
        if (failure == 3)
            dst.fail_link = 1;
        if (failure == 4)
            src.fail_map = 1;
        if (failure == 5)
            src.fail_walk = 1;
        if (failure == 6)
            dst.fail_link = 2;
        CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 0, &copied) != MM_CHUNK_OK && copied == 999);
        CHECK(dst.count == (failure == 4 ? 1U : 0U)); /* Legacy untracked new loner on map failure. */
        CHECK(!src.live && !dst.live && src.count == 3);
        dispose(&src);
        dispose(&dst);
    }
    init(&src);
    init(&dst);
    for (i = 0; i < 14; ++i)
        add(&src, i);
    for (i = 0; i < 13; ++i)
        link(&src, i, i + 1, i);
    src.fail_growth = 1;
    copied = 999;
    CHECK(mm_chunk_copy(&src.ops, &dst.ops, 10, 0, 1, &copied) == MM_CHUNK_MEMORY && copied == 999);
    CHECK(dst.count == 1 && dst.nodes[0].e.number == 10 && (dst.nodes[0].e.flags & 2U));
    dispose(&src);
    dispose(&dst);
}

static void copy_references(void)
{
    Host src, dst;
    MMU32 number, copied;
    int created;
    init(&src);
    init(&dst);
    graph(&src);
    add(&dst, 0);
    CHECK(mm_chunk_copy_reference(&src.ops, &src.ops, 10, 1, &number, &created) == MM_CHUNK_OK &&
          number == 1 && !created && !(src.nodes[1].e.flags & MM_CHUNK_LONER));
    src.fail_map = 1;
    number = 99;
    created = -1;
    CHECK(mm_chunk_copy_reference(&src.ops, &dst.ops, 10, 0, &number, &created) == MM_CHUNK_MEMORY &&
          dst.count == 1 && number == 99 && created == -1);
    src.fail_map = 0;
    CHECK(mm_chunk_copy_reference(&src.ops, &dst.ops, 10, 0, &number, &created) == MM_CHUNK_OK &&
          created && number != 0 && (dst.nodes[find(&dst, 10, number)].e.flags & MM_CHUNK_LONER));
    link(&dst, 0, number, 0);
    CHECK(mm_chunk_copy_reference(&src.ops, &dst.ops, 10, 0, &copied, &created) == MM_CHUNK_OK &&
          copied == number && !created && !(dst.nodes[find(&dst, 10, copied)].e.flags & MM_CHUNK_LONER) &&
          dst.nodes[find(&dst, 10, copied)].e.parent_count == 1 && dst.count == 4);
    dispose(&src);
    dispose(&dst);
}
static int stored_entry(void *c, MMU32 i, MMChunkEntry *out)
{
    entry(c, i, out);
    return 1;
}
static int stored_link(void *c, MMU32 parent, MMU32 i, MMChunkChild *out)
{
    child(c, parent, i, out);
    return 1;
}
static void stored_copies(void)
{
    Host src, dst;
    MMChunkAccess index;
    MMChunkEndpoint source = {0};
    MMU32 copied = 99, root, branch, shared;
    MMI32 highest = mm_chunk_runtime_highest();
    init(&src);
    init(&dst);
    graph(&src);
    add(&dst, 0);
    index.context = &src;
    index.entry = stored_entry;
    index.child = stored_link;
    index.count = src.count;
    source.identity = source.context = &src;
    source.allocator = &src.memory;
    source.stored = &index;
    source.read_payload = read_payload;
    source.read_name = read_name;

    src.fail_read = 2;
    CHECK(mm_chunk_copy(&source, &dst.ops, 10, 0, 0, &copied) == MM_CHUNK_IO);
    CHECK(copied == 99 && dst.count == 1 && dst.nodes[0].e.number == 0);
    CHECK(mm_chunk_copy(&source, &dst.ops, 10, 0, 0, &copied) == MM_CHUNK_OK);
    CHECK(dst.count == 4 && copied != 0 && mm_chunk_runtime_highest() == highest);
    root = find(&dst, 10, copied);
    branch = find(&dst, 10, dst.nodes[root].kids[0].number);
    shared = find(&dst, 10, dst.nodes[root].kids[1].number);
    CHECK(root < dst.count && branch < dst.count && shared < dst.count);
    CHECK(dst.nodes[root].e.flags & MM_CHUNK_FOREST);
    CHECK(dst.nodes[branch].kids[0].number == dst.nodes[shared].e.number);
    CHECK(dst.nodes[shared].e.parent_count == 2 && (dst.nodes[shared].e.flags & MM_CHUNK_PACKED));
    CHECK(!strcmp(dst.nodes[root].name, "named") && dst.physical[dst.nodes[shared].e.offset] == 1);
    dispose(&src);
    dispose(&dst);
}
static void live_clone_moves_rows(void)
{
    Host h;
    MMU32 copied, child_position;
    init(&h);
    add(&h, 0);
    add(&h, 1);
    h.nodes[0].e.number = 4;
    h.nodes[1].e.number = 7;
    link(&h, 4, 7, 0);
    memcpy(h.nodes[1].name, "glyph", 6);
    size(h.nodes + 1);
    CHECK(mm_chunk_copy(&h.ops, &h.ops, 10, 4, 1, &copied) == MM_CHUNK_OK && copied == 0);
    CHECK(!strcmp(h.nodes[0].name, "named") && h.nodes[0].e.child_count == 1);
    child_position = find(&h, 10, h.nodes[0].kids[0].number);
    CHECK(child_position < h.count && !strcmp(h.nodes[child_position].name, "glyph"));
    dispose(&h);
}
static void swaps_moves(void)
{
    Host h;
    MMChunkEntry a, b;
    int scan;
    MMU32 i;
    init(&h);
    graph(&h);
    add(&h, 3);
    a = h.nodes[0].e;
    b = h.nodes[1].e;
    CHECK(mm_chunk_swap_data(&h.ops.edit, 10, 0, 10, 1) == MM_CHUNK_OK);
    CHECK(h.nodes[0].e.offset == b.offset && h.nodes[1].e.offset == a.offset && h.nodes[0].e.child_count == 2 &&
          h.nodes[1].e.parent_count == 2);
    CHECK(h.nodes[0].e.flags == 6 && h.nodes[1].e.flags == 16 && h.resets == 2);
    CHECK(mm_chunk_swap_data(&h.ops.edit, 10, 0, 10, 0) == MM_CHUNK_OK && h.resets == 4);
    CHECK(mm_chunk_swap_children(&h.ops.edit, 10, 0, 10, 3) == MM_CHUNK_OK &&
          h.nodes[0].e.child_count == 0 && h.nodes[3].e.child_count == 2);
    CHECK(h.nodes[3].kids[0].number == 2 && !strcmp(h.nodes[3].name, "named") && h.resets == 4);
    CHECK(mm_chunk_swap_children(&h.ops.edit, 10, 0, 10, 0) == MM_CHUNK_BAD_FILE);
    dispose(&h);
    for (scan = 0; scan < 2; ++scan)
    {
        init(&h);
        graph(&h);
        h.ops.scan_all_references = scan;
        h.nodes[1].runtime = 99;
        h.nodes[2].runtime = 88;
        CHECK(mm_chunk_move(&h.ops, 10, 1, 11, 9) == MM_CHUNK_OK);
        i = find(&h, 11, 9);
        CHECK(i < h.count && !h.nodes[i].runtime && h.nodes[i].e.parent_count == 2);
        CHECK(h.nodes[0].kids[1].type == 11 && h.nodes[1].kids[0].type == 11);
        CHECK(mm_chunk_move(&h.ops, 10, 2, 10, 20) == MM_CHUNK_OK && runtime(&h, 10, 20) == 88);
        CHECK(mm_chunk_move(&h.ops, 10, 20, 10, 2) == MM_CHUNK_OK && runtime(&h, 10, 2) == 88);
        CHECK(mm_chunk_move(&h.ops, 10, 2, 10, 0) == MM_CHUNK_NOT_FOUND);
        dispose(&h);
    }
}
int main(void)
{
    copies();
    copy_references();
    stored_copies();
    live_clone_moves_rows();
    swaps_moves();
    return 0;
}
