#include "mm/chunky_file.h"
#include "mm/native_files.h"
#include <stdio.h>
#include <string.h>

static int check_index(void *file, MMU32 type)
{
    const unsigned short native_order = 1;
    MMChunkPolicy policy = {0, 0, 516}; /* Legacy counted name plus terminator and encoding. */
    MMReadStream stream;
    MMChunkIndex *index = NULL;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMReadSlice payload;
    MMU32 position;
    unsigned char magic[4], prefix[4];
    int ok;

    policy.native_big_endian = *(const unsigned char *)&native_order == 0;
    stream.length = 123;
    if (mm_native_file_read_stream(NULL, &stream) || stream.length != 123 ||
        !mm_native_file_read_stream(file, &stream) ||
        !mm_read_exact(&stream, 0, magic, sizeof(magic)) ||
        mm_read_exact(&stream, stream.length, prefix, 1) ||
        mm_chunk_index_open(&stream, &policy, &index) != MM_CHUNK_OK)
        return 0;

    access = mm_chunk_index_access(index);
    ok = mm_chunk_type_at(&access, type, 0, &entry, &position) == MM_CHUNK_OK &&
         mm_read_slice_init(&payload, &stream, entry.offset, entry.bytes) &&
         mm_read_exact(&payload.stream, 0, prefix, sizeof(prefix)) &&
         mm_read_exact(&stream, 0, prefix, sizeof(prefix)) &&
         memcmp(magic, prefix, sizeof(magic)) == 0;
    if (ok)
        printf("%lu indexed chunks, first requested type at chunk 0x%08lx (%lu stored bytes)\n",
               (unsigned long)access.count, (unsigned long)entry.number, (unsigned long)entry.bytes);
    mm_chunk_index_close(index);
    return ok;
}

int main(int argc, char **argv)
{
    MMNativeOpenOptions options;
    MMU32 type = 0;
    void *file;
    int i, ok;

    if (argc != 3 || strlen(argv[2]) != 4)
    {
        fputs("Usage: engine-native-chunky-test FILE TYPE\n", stderr);
        return 1;
    }
    for (i = 0; i < 4; ++i)
        type = (type << 8) | (unsigned char)argv[2][i];
    mm_native_open_options_init(&options);
    file = mm_native_file_open(argv[1], &options);
    if (!file)
    {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }
    ok = check_index(file, type);
    mm_native_file_close(file);
    if (!ok)
        fprintf(stderr, "Cannot read a %s chunk from %s\n", argv[2], argv[1]);
    return ok ? 0 : 1;
}
