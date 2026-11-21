#include "mm/chunk_storage.h"
#include "mm/movie_archive_edit.h"
#include "chunk_writer_fixture.h"
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
    MMU32 ends[2], trace_kind[8], trace_offset[8];
    MMChunkEntry row;
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
static int reserve(void *c, int extra, MMU32 bytes, MMU32 *offset)
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
static void release_space(void *c, int extra, MMU32 offset, MMU32 bytes)
{
    Host *h = (Host *)c;
    CHECK(extra && h->extra && h->ends[1] == offset + bytes);
    ++h->frees;
    h->ends[1] = offset;
}
static void entry(void *c, MMU32 position, MMChunkEntry *row)
{
    Host *h = (Host *)c;
    CHECK(position == 2);
    ++h->reads;
    *row = h->row;
}
static void place(void *c, MMU32 position, MMU32 offset)
{
    Host *h = (Host *)c;
    CHECK(position == 2 && h->extra && !h->frees);
    ++h->placements;
    h->row.offset = offset;
    h->row.flags |= 1;
}
static int copy(void *c, const MMChunkLocation *from, const MMChunkLocation *to)
{
    Host *h = (Host *)c;
    CHECK(from->file == &h->main_token && to->file == &h->extra_token && from->offset == h->row.offset &&
          from->bytes == h->row.bytes && to->bytes == from->bytes && !from->flags && to->flags == 1);
    ++h->copies;
    return !h->fail_copy;
}
static void trace(void *c, int kind, const MMChunkEntry *row)
{
    Host *h = (Host *)c;
    CHECK(h->traces < 8);
    h->trace_kind[h->traces] = (MMU32)kind;
    h->trace_offset[h->traces] = row->offset;
    ++h->traces;
}

typedef struct PayloadFixture
{
    ChunkWriterFixture archive;
    unsigned char file[256];
    MMU32 allocated, freed_offset;
    int decode_calls, writes, fail_decode, fail_allocate, fail_write;
} PayloadFixture;

static const char packed_bytes[] = "packed bytes";
enum { packed_length = sizeof(packed_bytes) - 1 };

static int payload_extent(void *p, MMU32 bytes, MMChunkLocation *out)
{
    PayloadFixture *f = ((ChunkIndexFixture *)p)->observer;
    if (f->fail_allocate)
        return 0;
    CHECK(bytes <= sizeof(f->file) - f->allocated);
    out->file = &f->archive;
    out->offset = f->allocated;
    out->bytes = bytes;
    out->flags = 0;
    f->allocated += bytes;
    return 1;
}
static void payload_free(void *p, int extra, MMU32 offset, MMU32 bytes)
{
    PayloadFixture *f = ((ChunkIndexFixture *)p)->observer;
    CHECK(!extra && bytes && offset + bytes <= sizeof(f->file));
    f->freed_offset = offset;
}
static int payload_read(void *p, void *file, MMU32 offset, void *out, size_t bytes)
{
    PayloadFixture *f = ((ChunkWriterFixture *)p)->context;
    CHECK(file == p && offset <= sizeof(f->file) && bytes <= sizeof(f->file) - offset);
    memcpy(out, f->file + offset, bytes);
    return 1;
}
static int payload_write(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    PayloadFixture *f = ((ChunkWriterFixture *)p)->context;
    CHECK(file == p && offset <= sizeof(f->file) && bytes <= sizeof(f->file) - offset);
    if (++f->writes == f->fail_write)
        return 0;
    memcpy(f->file + offset, data, bytes);
    return 1;
}
static void payload_retain(void *p, void *file)
{
    CHECK(file == p);
    ++((ChunkWriterFixture *)p)->file_refs;
}
static void payload_release(void *p, void *file)
{
    ChunkWriterFixture *f = p;
    CHECK(file == p && f->file_refs > 0);
    --f->file_refs;
}
static int payload_locate(void *p, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    ChunkWriterFixture *f = p;
    MMChunkAccess chunks = fixture_chunk_access(&f->index);
    MMChunkEntry entry;
    MMU32 row;
    if (mm_chunk_find(&chunks, type, number, &row) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, row, &entry) != MM_CHUNK_OK)
        return 0;
    out->file = f;
    out->offset = entry.offset;
    out->length = entry.bytes;
    *packed = (entry.flags & MM_CHUNK_PACKED) != 0;
    return 1;
}
static int payload_decode(void *p, void **handle)
{
    PayloadFixture *f = p;
    static const char decoded[] = "abcdefgh";
    ++f->decode_calls;
    CHECK(resource_handle_size(NULL, *handle) == packed_length &&
          !memcmp(resource_handle_lock(NULL, *handle), packed_bytes, packed_length));
    if (f->fail_decode)
        return 0;
    resource_block_decode(handle, decoded, sizeof(decoded) - 1);
    return 1;
}
static int serialize_payload(void *p, const MMStorageWriter *writer)
{
    PayloadFixture *f = p;
    CHECK(f->archive.file_refs == (f->archive.index.rows[0].entry.flags & MM_CHUNK_PACKED ? 1 : 2) &&
          writer->length == 8);
    return writer->write_at(writer->context, 0, "ABCD", 4) &&
           writer->write_at(writer->context, 4, "EFGH", 4);
}
static void archive_payloads(void)
{
    const MMU32 type = 22, number = 4, initial = 16;
    PayloadFixture f;
    MMMovieArchiveFile binding;
    MMArchiveWriter archive;
    MMChunkTitleIO titles = {0};
    MMMovieArchiveEditor editor;
    MMReadStream input;
    MMChunkEntry *row;
    MMChunkChild child = {23, 1, 0};
    char bytes[8];
    int mode;
    for (mode = 0; mode < 8; ++mode)
    {
        memset(&f, 0, sizeof(f));
        f.allocated = 64;
        archive = chunk_writer_fixture(&f.archive, &f, payload_extent, payload_write);
        f.archive.block.files.read = payload_read;
        f.archive.block.files.retain = payload_retain;
        f.archive.block.files.release = payload_release;
        f.archive.payloads.locate = payload_locate;
        f.archive.payloads.decode_context = &f;
        f.archive.payloads.decode = payload_decode;
        archive.edit.free_space = payload_free;
        row = &fixture_chunk_add(&f.archive.index, type, number)->entry;
        row->offset = initial;
        row->bytes = mode ? packed_length : 8;
        if (mode)
            row->flags |= MM_CHUNK_PACKED;
        memcpy(f.file + initial, mode ? packed_bytes : "abcdefgh", row->bytes);
        fixture_chunk_add(&f.archive.index, child.type, child.number);
        CHECK(mm_chunk_adopt(&archive.edit, type, number, &child, 1) == MM_CHUNK_OK);
        CHECK(fixture_chunk_set_runtime(&f.archive.index, type, number, 75));
        CHECK(mm_movie_archive_file_init(&binding, &f.archive, &archive, &titles));
        editor = mm_movie_archive_file_editor(&binding, 1, NULL);
        if (mode == 0)
        {
            CHECK(editor.read(editor.context, type, number, &input) &&
                  mm_read_exact(&input, 0, bytes, sizeof(bytes)) && !memcmp(bytes, "abcdefgh", 8));
            CHECK(f.archive.file_refs == 1);
            CHECK(editor.patch(editor.context, type, number, 3, "XY", 2) && !f.decode_calls);
            CHECK(!editor.patch(editor.context, type, number, 7, "XY", 2));
            CHECK(row->offset == initial && row->bytes == 8 && !memcmp(f.file + initial, "abcXYfgh", 8));
            f.fail_write = f.writes + 2;
            CHECK(!editor.replace(editor.context, type, number, 8, serialize_payload, &f));
            CHECK(row->offset == initial && f.freed_offset == 64 && row->child_count == 1 &&
                  fixture_chunk_runtime(&f.archive.index, type, number) == 75 && f.archive.file_refs == 1);
            f.fail_write = 0;
            CHECK(editor.replace(editor.context, type, number, 8, serialize_payload, &f));
            CHECK(row->offset == 72 && row->bytes == 8 && row->flags == MM_CHUNK_LONER &&
                  f.freed_offset == initial && !memcmp(f.file + 72, "ABCDEFGH", 8));
        }
        else if (mode <= 5)
        {
            f.fail_decode = mode == 1;
            f.fail_allocate = mode == 2;
            f.fail_write = mode == 3 ? 1 : 0;
            CHECK(editor.patch(editor.context, type, number, mode == 4 ? 7 : 3, "XY", 2) == (mode == 5));
            CHECK(f.decode_calls == 1 && !f.archive.file_refs && !memcmp(f.file + initial, packed_bytes, packed_length));
            if (mode == 5)
                CHECK(row->offset == 64 && row->bytes == 8 && row->flags == MM_CHUNK_LONER &&
                      f.freed_offset == initial && !memcmp(f.file + 64, "abcXYfgh", 8));
            else
                CHECK(row->offset == initial && row->bytes == packed_length && (row->flags & MM_CHUNK_PACKED) &&
                      f.freed_offset == (mode == 3 ? 64U : 0U));
        }
        else
        {
            f.fail_write = mode == 6 ? 2 : 0;
            CHECK(editor.replace(editor.context, type, number, 8, serialize_payload, &f) == (mode == 7));
            CHECK(!f.decode_calls && !f.archive.file_refs && !memcmp(f.file + initial, packed_bytes, packed_length));
            if (mode == 7)
                CHECK(row->offset == 64 && row->bytes == 8 && row->flags == MM_CHUNK_LONER &&
                      f.freed_offset == initial && !memcmp(f.file + 64, "ABCDEFGH", 8));
            else
                CHECK(row->offset == initial && row->bytes == packed_length && (row->flags & MM_CHUNK_PACKED) &&
                      f.freed_offset == 64);
        }
        CHECK(row->child_count == 1 && fixture_chunk_runtime(&f.archive.index, type, number) ==
              ((mode == 0 || mode == 5 || mode == 7) ? 0 : 75));
        mm_movie_archive_file_clear(&binding);
        CHECK(!f.archive.file_refs);
    }
}

int main(void)
{
    Host h;
    MMChunkStorageServices services;
    MMChunkStoragePolicy policy;
    MMChunkLocation out, sentinel;
    MMChunkEntry before;
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
            CHECK(mm_chunk_storage_allocate(&policy, &services, 0, (mode & 4) != 0, &out) == MM_CHUNK_OK);
            CHECK(out.file == &h.main_token && !out.offset && !out.bytes && !out.flags && !h.creates && !h.reserves);
        }
        else if (!extra && policy.free_map_unread)
            CHECK(mm_chunk_storage_allocate(&policy, &services, 17, 0, &out) == MM_CHUNK_BAD_FILE &&
                  !memcmp(&out, &sentinel, sizeof(out)) && !h.reserves);
        else
        {
            CHECK(mm_chunk_storage_allocate(&policy, &services, 17, (mode & 4) != 0, &out) == MM_CHUNK_OK);
            CHECK(out.file == (extra ? &h.extra_token : &h.main_token) && out.offset == (extra ? 0UL : 200UL) &&
                  out.bytes == 17 && out.flags == (MMU32)extra);
            CHECK(h.reserves == 1 && h.creates == extra);
        }
    }
    reset(&h);
    memset(&policy, 0, sizeof(policy));
    policy.max_bytes = 100;
    out = sentinel;
    CHECK(mm_chunk_storage_allocate(&policy, &services, 101, 1, &out) == MM_CHUNK_BAD_FILE && !h.creates &&
          !memcmp(&out, &sentinel, sizeof(out)));
    h.fail_create = 1;
    CHECK(mm_chunk_storage_allocate(&policy, &services, 17, 1, &out) == MM_CHUNK_IO && !h.extra &&
          !h.reserves && !memcmp(&out, &sentinel, sizeof(out)));
    reset(&h);
    h.fail_reserve = 1;
    CHECK(mm_chunk_storage_allocate(&policy, &services, 17, 1, &out) == MM_CHUNK_IO && h.extra &&
          h.creates == 1 && !h.ends[1] && !memcmp(&out, &sentinel, sizeof(out)));
    reset(&h);
    before = h.row;
    CHECK(mm_chunk_storage_cache(&policy, &services, 2, &out) == MM_CHUNK_OK);
    CHECK(out.file == &h.extra_token && out.bytes == 17 && !out.offset && out.flags == 1 && h.copies == 1 &&
          h.placements == 1 && !h.frees);
    before.offset = 0;
    before.flags |= 1;
    CHECK(!memcmp(&h.row, &before, sizeof(before)));
    CHECK(h.traces == 2 && h.trace_kind[0] == MM_STORAGE_CACHE &&
          h.trace_kind[1] == MM_STORAGE_FETCH_EXTRA && h.trace_offset[0] == 128 && !h.trace_offset[1]);
    h.traces = 0;
    CHECK(mm_chunk_storage_cache(&policy, &services, 2, NULL) == MM_CHUNK_OK && h.copies == 1 &&
          h.creates == 1 && !h.traces);
    reset(&h);
    h.row.bytes = 0;
    h.row.offset = 0;
    before = h.row;
    CHECK(mm_chunk_storage_cache(&policy, &services, 2, &out) == MM_CHUNK_OK && !out.file && !out.offset &&
          !out.bytes && !h.extra && !h.reserves && !memcmp(&h.row, &before, sizeof(before)));
    h.extra = 1;
    CHECK(mm_chunk_storage_cache(&policy, &services, 2, &out) == MM_CHUNK_OK &&
          out.file == &h.extra_token && !h.copies);
    reset(&h);
    before = h.row;
    h.fail_copy = 1;
    out = sentinel;
    CHECK(mm_chunk_storage_cache(&policy, &services, 2, &out) == MM_CHUNK_IO && h.frees == 1 &&
          !h.placements && !h.ends[1] && !memcmp(&h.row, &before, sizeof(before)) &&
          !memcmp(&out, &sentinel, sizeof(out)));
    reset(&h);
    h.fail_create = 1;
    out = sentinel;
    CHECK(mm_chunk_storage_cache(&policy, &services, 2, &out) == MM_CHUNK_IO && !h.copies && !h.frees &&
          !h.placements && !memcmp(&out, &sentinel, sizeof(out)));
    reset(&h);
    h.fail_reserve = 1;
    out = sentinel;
    CHECK(mm_chunk_storage_cache(&policy, &services, 2, &out) == MM_CHUNK_IO && h.extra && !h.copies &&
          !h.frees && !h.placements && !memcmp(&out, &sentinel, sizeof(out)));
    /* Failed read-through caching falls back, and a later fetch retries. */
    reset(&h);
    policy.read_from_extra = 1;
    before = h.row;
    h.fail_copy = 1;
    CHECK(mm_chunk_storage_fetch(&policy, &services, 2, &out) == MM_CHUNK_OK && out.file == &h.main_token &&
          out.offset == 128 && h.frees == 1 && !memcmp(&h.row, &before, sizeof(before)));
    CHECK(h.traces == 2 && h.trace_kind[0] == MM_STORAGE_CACHE &&
          h.trace_kind[1] == MM_STORAGE_FETCH_DIRECT);
    h.fail_copy = 0;
    h.traces = 0;
    h.frees = 0;
    CHECK(mm_chunk_storage_fetch(&policy, &services, 2, &out) == MM_CHUNK_OK &&
          out.file == &h.extra_token && !out.offset && h.creates == 1 && h.copies == 2 && h.placements == 1);
    reset(&h);
    policy.read_from_extra = 0;
    CHECK(mm_chunk_storage_fetch(&policy, &services, 2, &out) == MM_CHUNK_OK && out.file == &h.main_token &&
          !h.copies && h.trace_kind[0] == MM_STORAGE_FETCH_DIRECT);
    h.extra = 1;
    h.row.flags |= 1;
    CHECK(mm_chunk_storage_fetch(&policy, &services, 2, &out) == MM_CHUNK_OK &&
          out.file == &h.extra_token && out.flags == 1 && !h.copies);
    reset(&h);
    policy.read_from_extra = 1;
    h.row.bytes = 0;
    h.row.offset = 0;
    CHECK(mm_chunk_storage_fetch(&policy, &services, 2, &out) == MM_CHUNK_OK && !out.file && !h.creates &&
          !(h.row.flags & 1));
    reset(&h);
    policy.read_from_extra = 1;
    h.fail_reserve = 1;
    CHECK(mm_chunk_storage_fetch(&policy, &services, 2, &out) == MM_CHUNK_OK && out.file == &h.main_token &&
          h.extra && !h.copies);
    services.trace = NULL;
    h.fail_reserve = 0;
    CHECK(mm_chunk_storage_fetch(&policy, &services, 2, &out) == MM_CHUNK_OK && out.file == &h.extra_token);
    archive_payloads();
    return 0;
}
