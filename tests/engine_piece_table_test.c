#include "three_mm/piece_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "piece check %d: %s\n", __LINE__, #x);                                                     \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Host
{
    int files, buffers, calls, fail, fail_all, read_fail, copy_fail, write_fail, temp_fail;
} Host;
typedef struct File
{
    int refs;
    ThreeMMU32 length;
    unsigned char bytes[30000];
} File;
static void *resize(void *context, void *old, ThreeMMU32 old_size, ThreeMMU32 n, int priority)
{
    Host *h = (Host *)context;
    void *p;
    (void)priority;
    if (++h->calls == h->fail || (h->fail_all && h->calls >= h->fail))
        return NULL;
    p = malloc(n);
    CHECK(p);
    if (old)
    {
        memcpy(p, old, old_size < n ? old_size : n);
        free(old);
    }
    else
        ++h->buffers;
    return p;
}
static void free_buffer(void *context, void *p, ThreeMMU32 n)
{
    Host *h = (Host *)context;
    (void)n;
    --h->buffers;
    free(p);
}
static void *create(void *context)
{
    Host *h = (Host *)context;
    File *f;
    if (h->temp_fail)
    {
        h->temp_fail = 0;
        return NULL;
    }
    f = (File *)malloc(sizeof(*f));
    CHECK(f);
    f->refs = 1;
    f->length = 0;
    ++h->files;
    return f;
}
static void retain(void *context, void *file)
{
    (void)context;
    ++((File *)file)->refs;
}
static void release(void *context, void *file)
{
    Host *h = (Host *)context;
    File *f = (File *)file;
    CHECK(f->refs > 0);
    if (!--f->refs)
    {
        --h->files;
        free(f);
    }
}
static int read_file(void *context, const ThreeMMFilePiece *p, void *output)
{
    Host *h = (Host *)context;
    File *f = (File *)p->file;
    if (h->read_fail)
    {
        h->read_fail = 0;
        return 0;
    }
    CHECK(three_mm_range_contains(f->length, p->offset, p->length));
    memcpy(output, f->bytes + p->offset, p->length);
    return 1;
}
static int write_file(void *context, const ThreeMMFilePiece *p, const void *input)
{
    Host *h = (Host *)context;
    File *f = (File *)p->file;
    if (h->write_fail)
    {
        h->write_fail = 0;
        return 0;
    }
    CHECK(three_mm_range_contains(sizeof(f->bytes), p->offset, p->length));
    memcpy(f->bytes + p->offset, input, p->length);
    if (p->offset + p->length > f->length)
        f->length = p->offset + p->length;
    return 1;
}
static int copy_file(void *context, const ThreeMMFilePiece *from, const ThreeMMFilePiece *to)
{
    Host *h = (Host *)context;
    File *f = (File *)from->file;
    if (h->copy_fail)
    {
        h->copy_fail = 0;
        return 0;
    }
    CHECK(from->length == to->length && three_mm_range_contains(f->length, from->offset, from->length));
    return write_file(context, to, f->bytes + from->offset);
}
static void init(ThreeMMPieceTable *t, Host *h)
{
    ThreeMMStorageAllocator a;
    ThreeMMPieceFiles files;
    a.context = h;
    a.resize = resize;
    a.release = free_buffer;
    files.context = h;
    files.retain = retain;
    files.release = release;
    files.create_temp = create;
    files.read = read_file;
    files.write = write_file;
    files.copy = copy_file;
    CHECK(three_mm_piece_table_init(t, &a, &files, 1) == THREE_MM_DATA_OK);
}
static void check_bytes(ThreeMMPieceTable *t, const unsigned char *expected, ThreeMMU32 n)
{
    unsigned char actual[30000];
    ThreeMMFilePiece p;
    ThreeMMU32 i, total = 0;
    CHECK(t->length == n && three_mm_piece_table_read(t, 0, actual, n) == THREE_MM_DATA_OK &&
          !memcmp(actual, expected, n));
    for (i = 0; i < t->group.count; ++i)
    {
        CHECK(three_mm_piece_table_get(t, i, &p, NULL) == THREE_MM_DATA_OK);
        CHECK(p.length);
        total += p.length;
    }
    CHECK(total == n);
}
static ThreeMMU32 hash = 2166136261UL;
static void hash_byte(unsigned char n)
{
    hash = (hash ^ n) * 16777619UL;
}
static void golden(void)
{
    Host h;
    File *file;
    ThreeMMPieceTable t, src;
    ThreeMMFilePiece whole, small, piece;
    unsigned char model[30000], source[8216], insert[16];
    ThreeMMU32 i, state = 12345, offset, remove, n, length = 0, from;
    int step;
    const unsigned char *bytes;
    memset(&h, 0, sizeof(h));
    init(&t, &h);
    init(&src, &h);
    file = (File *)create(&h);
    file->length = 8192;
    for (i = 0; i < 8192; ++i)
        file->bytes[i] = (unsigned char)(i * 37 + i / 256);
    whole.file = file;
    whole.offset = 0;
    whole.length = 8192;
    small = whole;
    small.offset = 47;
    small.length = 31;
    CHECK(three_mm_piece_table_replace(&src, 0, 0, file->bytes, 13) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_replace_file(&src, 13, 0, &whole, 0) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_replace(&src, src.length, 0, file->bytes + 20, 11) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_read(&src, 0, source, src.length) == THREE_MM_DATA_OK);
    for (step = 0; step < 180; ++step)
    {
        state = state * 1664525UL + 1013904223UL;
        offset = state % (length + 1);
        remove = length ? (state >> 16) % (length - offset + 1) : 0;
        if (step % 7 == 0)
        {
            piece = whole;
            piece.offset = step % 256;
            piece.length = 2048;
            n = piece.length;
            bytes = file->bytes + piece.offset;
            CHECK(three_mm_piece_table_replace_file(&t, offset, remove, &piece, step % 2 != 0) == THREE_MM_DATA_OK);
        }
        else if (step % 7 == 1)
        {
            n = small.length;
            bytes = file->bytes + small.offset;
            CHECK(three_mm_piece_table_replace_file(&t, offset, remove, &small, 1) == THREE_MM_DATA_OK);
        }
        else if (step % 7 == 2)
        {
            from = step % 17;
            n = 4096 + step;
            bytes = source + from;
            CHECK(three_mm_piece_table_replace_table(&t, offset, remove, &src, from, n) == THREE_MM_DATA_OK);
        }
        else
        {
            n = step % 16;
            for (i = 0; i < n; ++i)
                insert[i] = (unsigned char)(step + i);
            bytes = insert;
            CHECK(three_mm_piece_table_replace(&t, offset, remove, insert, n) == THREE_MM_DATA_OK);
        }
        CHECK(length - remove + n < sizeof(model));
        memmove(model + offset + n, model + offset + remove, length - offset - remove);
        memcpy(model + offset, bytes, n);
        length = length - remove + n;
        check_bytes(&t, model, length);
        for (i = 0; i < 4; ++i)
            hash_byte((unsigned char)(length >> (8 * i)));
        for (i = 0; i < length; ++i)
            hash_byte(model[i]);
    }
    /* Frozen from 601,476 bytes captured with the pre-conversion native stream. */
    CHECK(hash == 0x84066842UL);
    CHECK(three_mm_piece_table_compact(&t) == THREE_MM_DATA_OK);
    check_bytes(&t, model, length);
    /* Single-file compaction must neither index piece 1 nor create a new file. */
    h.temp_fail = 1;
    CHECK(three_mm_piece_table_compact(&t) == THREE_MM_DATA_OK && h.temp_fail == 1);
    h.temp_fail = 0;
    CHECK(three_mm_piece_table_replace_table(&t, 0, t.length, &src, 0, src.length) == THREE_MM_DATA_OK);
    check_bytes(&t, source, src.length);
    CHECK(three_mm_piece_table_dispose(&t) == THREE_MM_DATA_OK &&
          three_mm_piece_table_dispose(&src) == THREE_MM_DATA_OK);
    release(&h, file);
    CHECK(!h.files && !h.buffers);
}
static void failures(void)
{
    Host h;
    ThreeMMPieceTable t, src;
    File *file;
    ThreeMMFilePiece piece;
    unsigned char bytes[8192], expected[8192];
    int point, result, operation;
    ThreeMMU32 i;
    for (i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (unsigned char)(i * 17);
    for (operation = 0; operation < 3; ++operation)
        for (point = 1; point <= 12; ++point)
        {
            memset(&h, 0, sizeof(h));
            init(&t, &h);
            init(&src, &h);
            file = (File *)create(&h);
            memcpy(file->bytes, bytes, sizeof(bytes));
            file->length = sizeof(bytes);
            piece.file = file;
            piece.offset = 0;
            piece.length = 4096;
            CHECK(three_mm_piece_table_replace_file(&t, 0, 0, &piece, 0) == THREE_MM_DATA_OK);
            CHECK(three_mm_piece_table_replace(&src, 0, 0, bytes, 17) == THREE_MM_DATA_OK);
            CHECK(three_mm_piece_table_replace_file(&src, 17, 0, &piece, 0) == THREE_MM_DATA_OK);
            CHECK(three_mm_piece_table_replace(&src, src.length, 0, bytes, 9) == THREE_MM_DATA_OK);
            h.calls = 0;
            h.fail = point;
            h.fail_all = 1;
            if (operation == 0)
                result = three_mm_piece_table_replace(&t, 100, 31, bytes, 2048);
            else if (operation == 1)
                result = three_mm_piece_table_replace_file(&t, 100, 31, &piece, 0);
            else
                result = three_mm_piece_table_replace_table(&t, 100, 31, &src, 3, 4110);
            h.fail = 0;
            h.fail_all = 0;
            if (result != THREE_MM_DATA_OK)
                check_bytes(&t, bytes, 4096);
            else
            {
                memcpy(expected, bytes, 100);
                if (operation == 0)
                {
                    memcpy(expected + 100, bytes, 2048);
                    memcpy(expected + 2148, bytes + 131, 3965);
                    check_bytes(&t, expected, 6113);
                }
                else if (operation == 1)
                {
                    memcpy(expected + 100, bytes, 4096);
                    memcpy(expected + 4196, bytes + 131, 3965);
                    check_bytes(&t, expected, 8161);
                }
            }
            CHECK(three_mm_piece_table_dispose(&t) == THREE_MM_DATA_OK &&
                  three_mm_piece_table_dispose(&src) == THREE_MM_DATA_OK);
            release(&h, file);
            CHECK(!h.files && !h.buffers);
        }
}
static void io_failures(void)
{
    Host h;
    ThreeMMPieceTable t;
    ThreeMMFilePiece piece;
    File *file;
    unsigned char bytes[4096];
    int stage;
    memset(bytes, 0x41, sizeof(bytes));
    memset(&h, 0, sizeof(h));
    init(&t, &h);
    file = (File *)create(&h);
    memcpy(file->bytes, bytes, sizeof(bytes));
    file->length = sizeof(bytes);
    piece.file = file;
    piece.offset = 0;
    piece.length = 4096;
    CHECK(three_mm_piece_table_replace(&t, 0, 0, bytes, 4096) == THREE_MM_DATA_OK);
    for (stage = 0; stage < 3; ++stage)
    {
        if (stage == 0)
            h.temp_fail = 1;
        else if (stage == 1)
            h.write_fail = 1;
        else
        {
            h.fail = h.calls + 1;
            h.fail_all = 1;
        }
        CHECK(three_mm_piece_table_compact(&t) != THREE_MM_DATA_OK);
        h.fail = 0;
        h.fail_all = 0;
        check_bytes(&t, bytes, 4096);
        CHECK(h.files == 1);
    }
    h.copy_fail = 1;
    CHECK(three_mm_piece_table_replace_file(&t, 0, 0, &piece, 1) == THREE_MM_DATA_IO);
    check_bytes(&t, bytes, 4096);
    piece.length = 31;
    h.read_fail = 1;
    CHECK(three_mm_piece_table_replace_file(&t, 0, 0, &piece, 0) == THREE_MM_DATA_IO);
    check_bytes(&t, bytes, 4096);
    CHECK(three_mm_piece_table_replace(&t, 0, t.length, NULL, 0) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_compact(&t) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_replace(&t, 0, 0, bytes, 31) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_compact(&t) == THREE_MM_DATA_OK);
    check_bytes(&t, bytes, 31);
    CHECK(three_mm_piece_table_dispose(&t) == THREE_MM_DATA_OK);
    release(&h, file);
    CHECK(!h.files && !h.buffers);
}
static void self_append(void)
{
    Host h;
    ThreeMMPieceTable t;
    memset(&h, 0, sizeof(h));
    init(&t, &h);
    CHECK(three_mm_piece_table_replace(&t, 0, 0, "abcdef", 6) == THREE_MM_DATA_OK);
    CHECK(three_mm_piece_table_replace_table(&t, 6, 0, &t, 0, 3) == THREE_MM_DATA_OK);
    check_bytes(&t, (const unsigned char *)"abcdefabc", 9);
    CHECK(!t.storage.buffers[0].locks && !t.storage.buffers[1].locks);
    CHECK(three_mm_piece_table_dispose(&t) == THREE_MM_DATA_OK && !h.buffers && !h.files);
}
int main(void)
{
    golden();
    failures();
    io_failures();
    self_append();
    return 0;
}
