#ifndef ENGINE_MOVIE_GRAPH_H
#define ENGINE_MOVIE_GRAPH_H

#include "kauai_core.h"
#include "mm/movie_session.h"

namespace EngineMovieGraph
{
inline bool ReceivePayload(PChunkyFile file, MMU32 type, MMU32 number, MMReadReceiver receive, void *out)
{
    int result;
    return mm_chunk_read_payload(KauaiChunkPayloadServices(), file, type, number,
                                 receive, out, &result) == MM_CHUNK_OK && result;
}

inline int Payload(void *context, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    return mm_movie_block_payload(KauaiChunkPayloadServices(), context, entry, receive, out);
}

inline int Stored(void *context, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    int result;
    return mm_chunk_visit_stored(KauaiChunkPayloadServices(), context, entry->type, entry->number,
                                  receive, out, &result) == MM_CHUNK_OK ? result : MM_MOVIE_BAD_FILE;
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
    return mm_movie_block_read(KauaiChunkPayloadServices(), context, entry, bytes, size);
}

inline MMMovieSessionGraph FromFile(PChunkyFile file)
{
    return {file,
        [](void *p, MMChunkAccess *access) { *access = static_cast<PChunkyFile>(p)->ReadChunkAccess(); },
        Read, Payload, Name};
}
}
#endif
