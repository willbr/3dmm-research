/* Stream/block policy converted from Kauai stream.cpp, file.cpp and codec.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_DATA_STREAM_H
#define THREE_MM_ENGINE_DATA_STREAM_H
#include "collection_storage.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_DATA_OK,
        THREE_MM_DATA_ARGUMENT,
        THREE_MM_DATA_PACKED,
        THREE_MM_DATA_IO,
        THREE_MM_DATA_CODEC,
        THREE_MM_DATA_WRONG_SIZE,
        THREE_MM_DATA_MEMORY,
        THREE_MM_DATA_NOT_ENCODED
    };
    enum
    {
        THREE_MM_BLOCK_EMPTY,
        THREE_MM_BLOCK_MEMORY,
        THREE_MM_BLOCK_FILE
    };
    typedef struct ThreeMMBlockView
    {
        int kind, packed;
        void *memory;
        ThreeMMU32 start, length, capacity;
    } ThreeMMBlockView;
    int three_mm_range_contains(ThreeMMU32 length, ThreeMMU32 offset, ThreeMMU32 bytes);
    int three_mm_block_check(const ThreeMMBlockView *, ThreeMMU32 offset, ThreeMMU32 bytes, int packed_ok,
                             ThreeMMU32 *physical_offset);
    int three_mm_block_move_min(ThreeMMBlockView *, ThreeMMI32 delta);
    int three_mm_block_move_lim(ThreeMMBlockView *, ThreeMMI32 delta);
    /* Decode success consumes/replaces the handle. Failure preserves it. A file
     * read hands ownership of any non-NULL handle to this routine, even on failure.
     * size returns the valid decoded length (1..storage limit-1). The host adopts
     * the successful view and then releases its previous file reference. */
    typedef struct ThreeMMBlockDecoder
    {
        void *context;
        int (*read_file)(void *, void **handle);
        int (*decode)(void *, void **handle);
        void (*release)(void *, void *handle);
        ThreeMMU32 (*size)(void *, void *handle);
    } ThreeMMBlockDecoder;
    int three_mm_block_unpack(ThreeMMBlockView *, const ThreeMMBlockDecoder *);
    typedef struct ThreeMMPackedDecoder
    {
        void *context;
        void *(*find)(void *, ThreeMMI32 format);
        int (*convert)(void *codec, ThreeMMI32 format, const void *, ThreeMMU32 bytes, void *output,
                       ThreeMMU32 capacity, ThreeMMI32 *written);
    } ThreeMMPackedDecoder;
    /* Eight-byte big-endian format/decoded-size prefix. Query and conversion retain
     * the codec manager's output-size-on-failure convention. Find is called once;
     * the returned codec is borrowed. Compression algorithms remain host services. */
    int three_mm_packed_decode(const ThreeMMPackedDecoder *, const void *, ThreeMMU32 bytes, void *output,
                               ThreeMMU32 capacity, ThreeMMI32 *written);
    int three_mm_packed_format(const ThreeMMReadStream *, ThreeMMI32 *format);
    /* The same callback shape supplies encoder selection/conversion. Queries
     * retain the legacy input-size-minus-one estimate and minimum saving rule.
     * Conversion failures publish the callback count; pre-conversion failures
     * leave written untouched. */
    int three_mm_packed_encode(const ThreeMMPackedDecoder *encoder, ThreeMMI32 format, const void *, ThreeMMU32 bytes,
                               void *output, ThreeMMU32 capacity, ThreeMMI32 *written);

    /* Owns its C storage. No copying/re-entry; replacement input must not alias it.
     * MemoryByteStream's legacy reserve hints use NORMAL priority for both attempts,
     * including its nonzero shrink/growth-hint behavior. */
    typedef struct ThreeMMByteBuffer
    {
        ThreeMMCollectionStorage storage;
        ThreeMMU32 length, min_grow;
    } ThreeMMByteBuffer;
    int three_mm_byte_buffer_init(ThreeMMByteBuffer *, const ThreeMMStorageAllocator *);
    int three_mm_byte_buffer_reserve(ThreeMMByteBuffer *, ThreeMMU32 bytes, int shrink);
    int three_mm_byte_buffer_replace(ThreeMMByteBuffer *, ThreeMMU32 offset, ThreeMMU32 remove, const void *input,
                                     ThreeMMU32 bytes);
    int three_mm_byte_buffer_read(const ThreeMMByteBuffer *, ThreeMMU32 offset, void *output, ThreeMMU32 bytes);
    int three_mm_byte_buffer_write(ThreeMMByteBuffer *, ThreeMMU32 offset, const ThreeMMStorageWriter *);

    /* Borrow live piece metadata; callbacks may not edit/re-enter the stream. The
     * read API retains FileByteStream's 0xa3 fill for failed file segments, continues
     * later segments, and reports IO. The host supplies its existing warning. */
    typedef struct ThreeMMPieceReader
    {
        void *context;
        ThreeMMU32 count, length;
        int (*piece_size)(void *, ThreeMMU32 index, ThreeMMU32 *bytes);
        int (*read_piece)(void *, ThreeMMU32 index, ThreeMMU32 offset, void *, ThreeMMU32 bytes);
    } ThreeMMPieceReader;
    int three_mm_piece_find(const ThreeMMPieceReader *, ThreeMMU32 offset, ThreeMMU32 *index, ThreeMMU32 *start,
                            ThreeMMU32 *piece_bytes);
    int three_mm_piece_read(const ThreeMMPieceReader *, ThreeMMU32 offset, void *, ThreeMMU32 bytes);
#ifdef __cplusplus
}
#endif
#endif
