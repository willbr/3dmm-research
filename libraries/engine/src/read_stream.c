#include "three_mm/read_stream.h"
#include <string.h>

int three_mm_read_exact(const ThreeMMReadStream *stream, ThreeMMU32 offset, void *bytes, size_t count)
{
    if (!stream || offset > stream->length || count > stream->length - offset || (count && !bytes))
        return 0;
    if (!count)
        return 1;
    return stream->read_at && stream->read_at(stream->context, offset, bytes, count);
}
static int read_memory(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    const ThreeMMMemoryStream *memory = (const ThreeMMMemoryStream *)context;
    memcpy(bytes, memory->bytes + offset, count);
    return 1;
}
int three_mm_memory_stream_init(ThreeMMMemoryStream *memory, const void *bytes, size_t count)
{
    if (!memory || (count && !bytes) || count > (ThreeMMU32)0xffffffffUL)
        return 0;
    memory->bytes = (const unsigned char *)bytes;
    memory->stream.context = memory;
    memory->stream.length = (ThreeMMU32)count;
    memory->stream.read_at = read_memory;
    return 1;
}
static int read_slice(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    const ThreeMMReadSlice *slice = (const ThreeMMReadSlice *)context;
    return three_mm_read_exact(slice->parent, slice->offset + offset, bytes, count);
}
int three_mm_read_slice_init(ThreeMMReadSlice *slice, const ThreeMMReadStream *parent, ThreeMMU32 offset,
                             ThreeMMU32 count)
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
