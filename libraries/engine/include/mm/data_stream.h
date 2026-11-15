/* Stream/block policy converted from Kauai stream.cpp, file.cpp and codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_DATA_STREAM_H
#define MM_ENGINE_DATA_STREAM_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_DATA_OK,
        MM_DATA_ARGUMENT,
        MM_DATA_PACKED,
        MM_DATA_IO,
        MM_DATA_CODEC,
        MM_DATA_WRONG_SIZE,
        MM_DATA_MEMORY,
        MM_DATA_NOT_ENCODED
    };
    enum
    {
        MM_BLOCK_EMPTY,
        MM_BLOCK_MEMORY,
        MM_BLOCK_FILE
    };
    typedef struct MMBlockView
    {
        int kind, packed;
        void *memory;
        MMU32 start, length, capacity;
    } MMBlockView;
    int mm_range_contains(MMU32 length, MMU32 offset, MMU32 bytes);
    int mm_block_check(const MMBlockView *, MMU32 offset, MMU32 bytes, int packed_ok,
                             MMU32 *physical_offset);
    int mm_block_move_min(MMBlockView *, MMI32 delta);
    int mm_block_move_lim(MMBlockView *, MMI32 delta);
    /* Decode success consumes/replaces the handle. Failure preserves it. A file
     * read hands ownership of any non-NULL handle to this routine, even on failure.
     * size returns the valid decoded length (1..storage limit-1). The host adopts
     * the successful view and then releases its previous file reference. */
    typedef struct MMBlockDecoder
    {
        void *context;
        int (*read_file)(void *, void **handle);
        int (*decode)(void *, void **handle);
        void (*release)(void *, void *handle);
        MMU32 (*size)(void *, void *handle);
    } MMBlockDecoder;
    int mm_block_unpack(MMBlockView *, const MMBlockDecoder *);
    typedef struct MMPackedDecoder
    {
        void *context;
        void *(*find)(void *, MMI32 format);
        int (*convert)(void *codec, MMI32 format, const void *, MMU32 bytes, void *output,
                       MMU32 capacity, MMI32 *written);
    } MMPackedDecoder;
    /* Eight-byte big-endian format/decoded-size prefix. Query and conversion retain
     * the codec manager's output-size-on-failure convention. Find is called once;
     * the returned codec is borrowed. Compression algorithms remain host services. */
    int mm_packed_decode(const MMPackedDecoder *, const void *, MMU32 bytes, void *output,
                               MMU32 capacity, MMI32 *written);
    int mm_packed_format(const MMReadStream *, MMI32 *format);
    /* The same callback shape supplies encoder selection/conversion. Queries
     * retain the legacy input-size-minus-one estimate and minimum saving rule.
     * Conversion failures publish the callback count; pre-conversion failures
     * leave written untouched. */
    int mm_packed_encode(const MMPackedDecoder *encoder, MMI32 format, const void *, MMU32 bytes,
                               void *output, MMU32 capacity, MMI32 *written);

    /* Owns its C storage. No copying/re-entry; replacement input must not alias it.
     * MemoryByteStream's legacy reserve hints use NORMAL priority for both attempts,
     * including its nonzero shrink/growth-hint behavior. */
    typedef struct MMByteBuffer
    {
        MMCollectionStorage storage;
        MMU32 length, min_grow;
    } MMByteBuffer;
    int mm_byte_buffer_init(MMByteBuffer *, const MMStorageAllocator *);
    int mm_byte_buffer_reserve(MMByteBuffer *, MMU32 bytes, int shrink);
    int mm_byte_buffer_replace(MMByteBuffer *, MMU32 offset, MMU32 remove, const void *input,
                                     MMU32 bytes);
    int mm_byte_buffer_read(const MMByteBuffer *, MMU32 offset, void *output, MMU32 bytes);
    int mm_byte_buffer_write(MMByteBuffer *, MMU32 offset, const MMStorageWriter *);

    /* Borrow live piece metadata; callbacks may not edit/re-enter the stream. The
     * read API retains FileByteStream's 0xa3 fill for failed file segments, continues
     * later segments, and reports IO. The host supplies its existing warning. */
    typedef struct MMPieceReader
    {
        void *context;
        MMU32 count, length;
        int (*piece_size)(void *, MMU32 index, MMU32 *bytes);
        int (*read_piece)(void *, MMU32 index, MMU32 offset, void *, MMU32 bytes);
    } MMPieceReader;
    int mm_piece_find(const MMPieceReader *, MMU32 offset, MMU32 *index, MMU32 *start,
                            MMU32 *piece_bytes);
    int mm_piece_read(const MMPieceReader *, MMU32 offset, void *, MMU32 bytes);
#ifdef __cplusplus
}
#endif
#endif
