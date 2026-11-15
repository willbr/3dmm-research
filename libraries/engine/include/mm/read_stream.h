#ifndef MM_ENGINE_READ_STREAM_H
#define MM_ENGINE_READ_STREAM_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif

    /* Borrowed decoded-chunk input. Reads are exact and positional: success means
     * all requested bytes were copied. Callbacks do not change stream ownership.
     * Chunk payload sizes are 32-bit; host file/VFS positions can remain wider.
     * Failed reads may modify the byte buffer. Record readers publish outputs only
     * after successful decoding. All stream/context pointers must remain valid. */
    typedef struct MMReadStream
    {
        void *context;
        MMU32 length;
        int (*read_at)(void *context, MMU32 offset, void *bytes, size_t count);
    } MMReadStream;

    typedef struct MMMemoryStream
    {
        MMReadStream stream;
        const unsigned char *bytes;
    } MMMemoryStream;
    typedef struct MMReadSlice
    {
        MMReadStream stream;
        const MMReadStream *parent;
        MMU32 offset;
    } MMReadSlice;

    int mm_read_exact(const MMReadStream *stream, MMU32 offset, void *bytes, size_t count);
    /* Init is transactional. Memory length must fit the wire-size range. Slices
     * borrow their parent and bound every nested read before invoking its callback.
     * Initialized memory/slice objects must not be moved (context points to self). */
    int mm_memory_stream_init(MMMemoryStream *memory, const void *bytes, size_t count);
    int mm_read_slice_init(MMReadSlice *slice, const MMReadStream *parent, MMU32 offset,
                                 MMU32 count);

#ifdef __cplusplus
}
#endif
#endif
