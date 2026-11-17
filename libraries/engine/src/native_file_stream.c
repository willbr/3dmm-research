#include "mm/native_files.h"

static int read_at(void *file, MMU32 offset, void *bytes, size_t count)
{
    return mm_native_file_seek(file, offset) && mm_native_file_read(file, bytes, (MMU32)count);
}

int mm_native_file_read_stream(void *file, MMReadStream *stream)
{
    MMU32 length;
    if (!file || !stream || !mm_native_file_size(file, &length))
        return 0;
    stream->context = file;
    stream->length = length;
    stream->read_at = read_at;
    return 1;
}
