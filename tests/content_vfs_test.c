#include "three_mm/content_vfs.h"
#include <stdio.h>
#include <string.h>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            fprintf(stderr, "VFS line %d: %s\n", __LINE__, #value);                                                    \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
typedef struct Memory
{
    int opened, closed;
    ThreeMMVfsPosition last;
} Memory;
static void *memory_open(void *context, const char *location)
{
    Memory *memory;
    memory = (Memory *)context;
    if (strcmp(location, "first"))
        return NULL;
    ++memory->opened;
    return memory;
}
static size_t memory_read(void *context, void *handle, ThreeMMVfsPosition position, void *buffer, size_t count)
{
    Memory *memory;
    unsigned char *bytes;
    size_t i;
    memory = (Memory *)context;
    if (handle != context)
        return 0;
    memory->last = position;
    bytes = (unsigned char *)buffer;
    for (i = 0; i < count; ++i)
        bytes[i] = (unsigned char)((position.low + i) & 255);
    return count;
}
static void memory_close(void *context, void *handle)
{
    Memory *memory;
    memory = (Memory *)context;
    if (handle == context)
        ++memory->closed;
}
int main(void)
{
    Memory memory;
    ThreeMMVfsProvider provider;
    ThreeMMVfs *vfs;
    ThreeMMVfsFile *file;
    ThreeMMVfsMount first[2], second, bad[2], entry;
    ThreeMMVfsPosition position;
    unsigned char bytes[4];
    char key[80];
    size_t cursor;
    memset(&memory, 0, sizeof(memory));
    provider.context = &memory;
    provider.open = memory_open;
    provider.read_at = memory_read;
    provider.close = memory_close;
    vfs = three_mm_vfs_create(&provider);
    CHECK(vfs);
    CHECK(three_mm_vfs_source_key("Localized product/3DMovie", NULL, key, sizeof(key)) && !strcmp(key, "3dmovie"));
    CHECK(three_mm_vfs_source_key("Nickelodeon", "N3DMMExp", key, sizeof(key)) && !strcmp(key, "n3dmmexp"));
    CHECK(three_mm_vfs_source_key("Custom package", "CUSTOM", key, sizeof(key)) && !strcmp(key, "custom"));
    CHECK(!three_mm_vfs_source_key("3dmm", NULL, key, 4));
    first[0].name = "B.3cn";
    first[0].location = "first";
    first[1].name = "A.3th";
    first[1].location = "thumbnail";
    CHECK(three_mm_vfs_mount(vfs, "Virtual3dmm/v3dmm", first, 2, 0) == THREE_MM_VFS_OK);
    second.name = "B.3cn";
    second.location = "second";
    CHECK(three_mm_vfs_mount(vfs, "v3dmm", &second, 1, 0) == THREE_MM_VFS_OK);
    CHECK(!strcmp(three_mm_vfs_resolve(vfs, "V3DMM", "b.3CN"), "first"));
    CHECK(three_mm_vfs_mount(vfs, "3dmovie", &second, 1, 0) == THREE_MM_VFS_OK);
    CHECK(!strcmp(three_mm_vfs_resolve(vfs, "3dmm", "B.3cn"), "second"));
    bad[0].name = "new.3cn";
    bad[0].location = "new";
    bad[1].name = "b.3cn";
    bad[1].location = "wrong-case";
    CHECK(three_mm_vfs_mount(vfs, "v3dmm", bad, 2, 0) == THREE_MM_VFS_CASE);
    CHECK(!three_mm_vfs_resolve(vfs, "v3dmm", "new.3cn"));
    bad[0].name = "../escape.3cn";
    CHECK(three_mm_vfs_mount(vfs, "v3dmm", bad, 1, 1) == THREE_MM_VFS_NAME);
    CHECK(!strcmp(three_mm_vfs_resolve(vfs, "v3dmm", "B.3cn"), "first"));
    cursor = 0;
    CHECK(three_mm_vfs_next(vfs, "v3dmm", &cursor, &entry) && !strcmp(entry.name, "A.3th"));
    CHECK(three_mm_vfs_next(vfs, "v3dmm", &cursor, &entry) && !strcmp(entry.name, "B.3cn"));
    CHECK(!three_mm_vfs_next(vfs, "v3dmm", &cursor, &entry));
    second.name = "caf\351.3cn";
    second.location = "first";
    CHECK(three_mm_vfs_mount(vfs, "v3dmm", &second, 1, 0) == THREE_MM_VFS_OK);
    CHECK(!strcmp(three_mm_vfs_resolve(vfs, "v3dmm", "caf\351.3CN"), "first"));
    file = three_mm_vfs_open(vfs, "v3dmm", "b.3cn");
    CHECK(file && memory.opened == 1);
    position.high = 0;
    position.low = 0xfffffffeUL;
    CHECK(three_mm_vfs_seek(file, position));
    CHECK(three_mm_vfs_read(file, bytes, 4) == 4 && bytes[0] == 254 && bytes[2] == 0);
    position = three_mm_vfs_tell(file);
    CHECK(position.high == 1 && position.low == 2 && memory.last.high == 0);
    CHECK(three_mm_vfs_mount(vfs, "v3dmm", NULL, 0, 1) == THREE_MM_VFS_OK);
    CHECK(!three_mm_vfs_mounted(vfs, "v3dmm") && three_mm_vfs_mounted(vfs, "3dmovie"));
    three_mm_vfs_destroy(vfs);
    CHECK(three_mm_vfs_read(file, bytes, 1) == 1 && memory.last.high == 1);
    three_mm_vfs_close(file);
    CHECK(memory.closed == 1);
    puts("C89 VFS: aliases, namespaces, precedence, atomic mounts, casing, traversal and 64-bit streams passed.");
    return 0;
}
