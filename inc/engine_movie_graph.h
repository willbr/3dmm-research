#ifndef ENGINE_MOVIE_GRAPH_H
#define ENGINE_MOVIE_GRAPH_H

#include "engine_read_stream.h"
#include "mm/movie_session.h"

namespace EngineMovieGraph
{
inline int Payload(void *context, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    DataBlock block;
    if (!static_cast<PChunkyFile>(context)->FFind(entry->type, entry->number, &block) || !block.FUnpackData())
        return MM_MOVIE_BAD_FILE;
    const auto stream = EngineReadStream::FromBlock(block);
    return receive(out, &stream);
}

inline int Name(void *context, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    unsigned char bytes[kcbMaxDataStn];
    MMU32 written;
    MMMemoryStream stream;
    if (!static_cast<PChunkyFile>(context)->FReadNameBytes(entry->type, entry->number, bytes, sizeof(bytes), &written) ||
        !mm_memory_stream_init(&stream, bytes, written))
        return MM_MOVIE_BAD_FILE;
    return receive(out, &stream.stream);
}

inline int Read(void *context, void *, MMU32, const MMChunkEntry *entry, void *bytes, MMU32 size)
{
    struct Record { void *bytes; MMU32 size; } record = {bytes, size};
    return Payload(context, entry, [](void *p, const MMReadStream *stream) -> int {
        auto &r = *static_cast<Record *>(p);
        if (stream->length != r.size)
            return MM_MOVIE_BAD_FILE;
        return mm_read_exact(stream, 0, r.bytes, r.size) ? MM_MOVIE_OK : MM_MOVIE_IO;
    }, &record);
}

inline MMMovieSessionGraph FromFile(PChunkyFile file)
{
    return {file,
        [](void *p, MMChunkAccess *access) { *access = static_cast<PChunkyFile>(p)->ReadChunkAccess(); },
        Read, Payload, Name};
}
}
#endif
