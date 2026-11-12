#include "three_mm/chunk_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "storage routing failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Host
{
    int main_token, extra_token, extra, creates, reserves, copies, frees, placements, reads, traces;
    int fail_create, fail_reserve, fail_copy;
    ThreeMMU32 ends[2], trace_kind[8], trace_offset[8];
    ThreeMMChunkEntry row;
} Host;
static void reset(Host *h)
{
    memset(h, 0, sizeof(*h));
    h->ends[0] = 200;
    h->row.type = 0x41414141UL;
    h->row.number = 9;
    h->row.offset = 128;
    h->row.bytes = 17;
    h->row.flags = 22;
    h->row.child_count = 3;
    h->row.parent_count = 2;
    h->row.variable_bytes = 40;
}
static void *file(void *c, int extra)
{
    Host *h = (Host *)c;
    return extra ? (h->extra ? &h->extra_token : NULL) : &h->main_token;
}
static int create(void *c)
{
    Host *h = (Host *)c;
    CHECK(!h->extra);
    ++h->creates;
    if (h->fail_create)
        return 0;
    h->extra = 1;
    return 1;
}
static int reserve(void *c, int extra, ThreeMMU32 bytes, ThreeMMU32 *offset)
{
    Host *h = (Host *)c;
    CHECK(bytes > 0 && (!extra || h->extra));
    ++h->reserves;
    if (h->fail_reserve)
        return 0;
    *offset = h->ends[extra];
    h->ends[extra] += bytes;
    return 1;
}
static void release_space(void *c, int extra, ThreeMMU32 offset, ThreeMMU32 bytes)
{
    Host *h = (Host *)c;
    CHECK(extra && h->extra && h->ends[1] == offset + bytes);
    ++h->frees;
    h->ends[1] = offset;
}
static void entry(void *c, ThreeMMU32 position, ThreeMMChunkEntry *row)
{
    Host *h = (Host *)c;
    CHECK(position == 2);
    ++h->reads;
    *row = h->row;
}
static void place(void *c, ThreeMMU32 position, ThreeMMU32 offset)
{
    Host *h = (Host *)c;
    CHECK(position == 2 && h->extra && !h->frees);
    ++h->placements;
    h->row.offset = offset;
    h->row.flags |= 1;
}
static int copy(void *c, const ThreeMMChunkLocation *from, const ThreeMMChunkLocation *to)
{
    Host *h = (Host *)c;
    CHECK(from->file == &h->main_token && to->file == &h->extra_token && from->offset == h->row.offset &&
          from->bytes == h->row.bytes && to->bytes == from->bytes && !from->flags && to->flags == 1);
    ++h->copies;
    return !h->fail_copy;
}
static void trace(void *c, int kind, const ThreeMMChunkEntry *row)
{
    Host *h = (Host *)c;
    CHECK(h->traces < 8);
    h->trace_kind[h->traces] = (ThreeMMU32)kind;
    h->trace_offset[h->traces] = row->offset;
    ++h->traces;
}
int main(void)
{
    Host h;
    ThreeMMChunkStorageServices services;
    ThreeMMChunkStoragePolicy policy;
    ThreeMMChunkLocation out, sentinel;
    ThreeMMChunkEntry before;
    unsigned mode;
    int extra;
    memset(&services, 0, sizeof(services));
    services.context = &h;
    services.file = file;
    services.create_extra = create;
    services.reserve = reserve;
    services.release_space = release_space;
    services.entry = entry;
    services.cache_place = place;
    services.copy = copy;
    services.trace = trace;
    memset(&sentinel, 0x5a, sizeof(sentinel));
    for (mode = 0; mode < 16; ++mode)
    {
        reset(&h);
        memset(&policy, 0, sizeof(policy));
        policy.max_bytes = 100;
        policy.add_to_extra = (mode & 1) != 0;
        policy.free_map_unread = (mode & 2) != 0;
        extra = (mode & 4) != 0 || policy.add_to_extra;
        out = sentinel;
        if (mode & 8)
        {
            CHECK(three_mm_chunk_storage_allocate(&policy, &services, 0, (mode & 4) != 0, &out) == THREE_MM_CHUNK_OK);
            CHECK(out.file == &h.main_token && !out.offset && !out.bytes && !out.flags && !h.creates && !h.reserves);
        }
        else if (!extra && policy.free_map_unread)
            CHECK(three_mm_chunk_storage_allocate(&policy, &services, 17, 0, &out) == THREE_MM_CHUNK_BAD_FILE &&
                  !memcmp(&out, &sentinel, sizeof(out)) && !h.reserves);
        else
        {
            CHECK(three_mm_chunk_storage_allocate(&policy, &services, 17, (mode & 4) != 0, &out) == THREE_MM_CHUNK_OK);
            CHECK(out.file == (extra ? &h.extra_token : &h.main_token) && out.offset == (extra ? 0UL : 200UL) &&
                  out.bytes == 17 && out.flags == (ThreeMMU32)extra);
            CHECK(h.reserves == 1 && h.creates == extra);
        }
    }
    reset(&h);
    memset(&policy, 0, sizeof(policy));
    policy.max_bytes = 100;
    out = sentinel;
    CHECK(three_mm_chunk_storage_allocate(&policy, &services, 101, 1, &out) == THREE_MM_CHUNK_BAD_FILE && !h.creates &&
          !memcmp(&out, &sentinel, sizeof(out)));
    h.fail_create = 1;
    CHECK(three_mm_chunk_storage_allocate(&policy, &services, 17, 1, &out) == THREE_MM_CHUNK_IO && !h.extra &&
          !h.reserves && !memcmp(&out, &sentinel, sizeof(out)));
    reset(&h);
    h.fail_reserve = 1;
    CHECK(three_mm_chunk_storage_allocate(&policy, &services, 17, 1, &out) == THREE_MM_CHUNK_IO && h.extra &&
          h.creates == 1 && !h.ends[1] && !memcmp(&out, &sentinel, sizeof(out)));
    reset(&h);
    before = h.row;
    CHECK(three_mm_chunk_storage_cache(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK);
    CHECK(out.file == &h.extra_token && out.bytes == 17 && !out.offset && out.flags == 1 && h.copies == 1 &&
          h.placements == 1 && !h.frees);
    before.offset = 0;
    before.flags |= 1;
    CHECK(!memcmp(&h.row, &before, sizeof(before)));
    CHECK(h.traces == 2 && h.trace_kind[0] == THREE_MM_STORAGE_CACHE &&
          h.trace_kind[1] == THREE_MM_STORAGE_FETCH_EXTRA && h.trace_offset[0] == 128 && !h.trace_offset[1]);
    h.traces = 0;
    CHECK(three_mm_chunk_storage_cache(&policy, &services, 2, NULL) == THREE_MM_CHUNK_OK && h.copies == 1 &&
          h.creates == 1 && !h.traces);
    reset(&h);
    h.row.bytes = 0;
    h.row.offset = 0;
    before = h.row;
    CHECK(three_mm_chunk_storage_cache(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK && !out.file && !out.offset &&
          !out.bytes && !h.extra && !h.reserves && !memcmp(&h.row, &before, sizeof(before)));
    h.extra = 1;
    CHECK(three_mm_chunk_storage_cache(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK &&
          out.file == &h.extra_token && !h.copies);
    reset(&h);
    before = h.row;
    h.fail_copy = 1;
    out = sentinel;
    CHECK(three_mm_chunk_storage_cache(&policy, &services, 2, &out) == THREE_MM_CHUNK_IO && h.frees == 1 &&
          !h.placements && !h.ends[1] && !memcmp(&h.row, &before, sizeof(before)) &&
          !memcmp(&out, &sentinel, sizeof(out)));
    reset(&h);
    h.fail_create = 1;
    out = sentinel;
    CHECK(three_mm_chunk_storage_cache(&policy, &services, 2, &out) == THREE_MM_CHUNK_IO && !h.copies && !h.frees &&
          !h.placements && !memcmp(&out, &sentinel, sizeof(out)));
    reset(&h);
    h.fail_reserve = 1;
    out = sentinel;
    CHECK(three_mm_chunk_storage_cache(&policy, &services, 2, &out) == THREE_MM_CHUNK_IO && h.extra && !h.copies &&
          !h.frees && !h.placements && !memcmp(&out, &sentinel, sizeof(out)));
    /* Failed read-through caching falls back, and a later fetch retries. */
    reset(&h);
    policy.read_from_extra = 1;
    before = h.row;
    h.fail_copy = 1;
    CHECK(three_mm_chunk_storage_fetch(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK && out.file == &h.main_token &&
          out.offset == 128 && h.frees == 1 && !memcmp(&h.row, &before, sizeof(before)));
    CHECK(h.traces == 2 && h.trace_kind[0] == THREE_MM_STORAGE_CACHE &&
          h.trace_kind[1] == THREE_MM_STORAGE_FETCH_DIRECT);
    h.fail_copy = 0;
    h.traces = 0;
    h.frees = 0;
    CHECK(three_mm_chunk_storage_fetch(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK &&
          out.file == &h.extra_token && !out.offset && h.creates == 1 && h.copies == 2 && h.placements == 1);
    reset(&h);
    policy.read_from_extra = 0;
    CHECK(three_mm_chunk_storage_fetch(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK && out.file == &h.main_token &&
          !h.copies && h.trace_kind[0] == THREE_MM_STORAGE_FETCH_DIRECT);
    h.extra = 1;
    h.row.flags |= 1;
    CHECK(three_mm_chunk_storage_fetch(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK &&
          out.file == &h.extra_token && out.flags == 1 && !h.copies);
    reset(&h);
    policy.read_from_extra = 1;
    h.row.bytes = 0;
    h.row.offset = 0;
    CHECK(three_mm_chunk_storage_fetch(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK && !out.file && !h.creates &&
          !(h.row.flags & 1));
    reset(&h);
    policy.read_from_extra = 1;
    h.fail_reserve = 1;
    CHECK(three_mm_chunk_storage_fetch(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK && out.file == &h.main_token &&
          h.extra && !h.copies);
    services.trace = NULL;
    h.fail_reserve = 0;
    CHECK(three_mm_chunk_storage_fetch(&policy, &services, 2, &out) == THREE_MM_CHUNK_OK && out.file == &h.extra_token);
    return 0;
}