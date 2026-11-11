#ifndef THREE_MM_ENGINE_READ_STREAM_H
#define THREE_MM_ENGINE_READ_STREAM_H
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
    typedef struct ThreeMMReadStream
    {
        void *context;
        ThreeMMU32 length;
        int (*read_at)(void *context, ThreeMMU32 offset, void *bytes, size_t count);
    } ThreeMMReadStream;

    typedef struct ThreeMMMemoryStream
    {
        ThreeMMReadStream stream;
        const unsigned char *bytes;
    } ThreeMMMemoryStream;
    typedef struct ThreeMMReadSlice
    {
        ThreeMMReadStream stream;
        const ThreeMMReadStream *parent;
        ThreeMMU32 offset;
    } ThreeMMReadSlice;

    int three_mm_read_exact(const ThreeMMReadStream *stream, ThreeMMU32 offset, void *bytes, size_t count);
    /* Init is transactional. Memory length must fit the wire-size range. Slices
     * borrow their parent and bound every nested read before invoking its callback.
     * Initialized memory/slice objects must not be moved (context points to self). */
    int three_mm_memory_stream_init(ThreeMMMemoryStream *memory, const void *bytes, size_t count);
    int three_mm_read_slice_init(ThreeMMReadSlice *slice, const ThreeMMReadStream *parent, ThreeMMU32 offset,
                                 ThreeMMU32 count);

#ifdef __cplusplus
}
#endif
#endif
