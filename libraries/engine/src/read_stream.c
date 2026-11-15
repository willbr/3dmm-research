#include "mm/read_stream.h"
#include <string.h>

int mm_read_exact(const MMReadStream *stream, MMU32 offset, void *bytes, size_t count)
{
    if (!stream || offset > stream->length || count > stream->length - offset || (count && !bytes))
        return 0;
    if (!count)
        return 1;
    return stream->read_at && stream->read_at(stream->context, offset, bytes, count);
}
static int read_memory(void *context, MMU32 offset, void *bytes, size_t count)
{
    const MMMemoryStream *memory = (const MMMemoryStream *)context;
    memcpy(bytes, memory->bytes + offset, count);
    return 1;
}
int mm_memory_stream_init(MMMemoryStream *memory, const void *bytes, size_t count)
{
    if (!memory || (count && !bytes) || count > (MMU32)0xffffffffUL)
        return 0;
    memory->bytes = (const unsigned char *)bytes;
    memory->stream.context = memory;
    memory->stream.length = (MMU32)count;
    memory->stream.read_at = read_memory;
    return 1;
}
static int read_slice(void *context, MMU32 offset, void *bytes, size_t count)
{
    const MMReadSlice *slice = (const MMReadSlice *)context;
    return mm_read_exact(slice->parent, slice->offset + offset, bytes, count);
}
int mm_read_slice_init(MMReadSlice *slice, const MMReadStream *parent, MMU32 offset,
                             MMU32 count)
{
    if (!slice || !parent || &slice->stream == parent || offset > parent->length || count > parent->length - offset)
        return 0;
    slice->parent = parent;
    slice->offset = offset;
    slice->stream.context = slice;
    slice->stream.length = count;
    slice->stream.read_at = read_slice;
    return 1;
}
