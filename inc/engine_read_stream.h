#ifndef ENGINE_READ_STREAM_H
#define ENGINE_READ_STREAM_H
#include "kauai_core.h"
#include "three_mm/read_stream.h"

/* Transitional host adapter. DataBlock owns decoding/storage; the C backend
 * sees only bounded positional reads. The block must outlive the stream. */
namespace EngineReadStream
{
inline int ReadBlock(void *context, ThreeMMU32 offset, void *bytes, size_t count)
{
    if (offset > 0x7fffffffUL || count > 0x7fffffffUL)
        return 0;
    return static_cast<DataBlock *>(context)->FReadRgb(bytes, static_cast<LegacyLong>(count),
                                                       static_cast<LegacyLong>(offset));
}
inline ThreeMMReadStream FromBlock(DataBlock &block)
{
    return {&block, static_cast<ThreeMMU32>(block.Cb()), ReadBlock};
}
} // namespace EngineReadStream
#endif
