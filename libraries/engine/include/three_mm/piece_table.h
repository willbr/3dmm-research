/* Piece-table policy converted from Kauai stream.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_PIECE_TABLE_H
#define THREE_MM_ENGINE_PIECE_TABLE_H
#include "collections.h"
#include "data_stream.h"
#include "block_owner.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMFilePiece
    {
        void *file;
        ThreeMMU32 offset, length;
    } ThreeMMFilePiece;
    /* File identities are borrowed on input; each stored file piece owns a
     * reference. create_temp returns an owned reference. Copies between tables
     * require the same file ownership domain. Callbacks must not edit/re-enter
     * either table. read/write/copy are exact, positional operations. */
    typedef struct ThreeMMPieceFiles
    {
        void *context;
        void (*retain)(void *, void *file);
        void (*release)(void *, void *file);
        void *(*create_temp)(void *);
        int (*read)(void *, const ThreeMMFilePiece *, void *output);
        int (*write)(void *, const ThreeMMFilePiece *, const void *input);
        int (*copy)(void *, const ThreeMMFilePiece *, const ThreeMMFilePiece *);
    } ThreeMMPieceFiles;
    /* Owns C group storage and file references. Initialize before use; no copying.
     * Insertion inputs must not alias table storage. Failures can leave capacity
     * changes or equivalent splits; logical bytes and file references are retained.
     * Cross-table replacement inserts first and rolls back before deleting on error. */
    typedef struct ThreeMMPieceTable
    {
        ThreeMMCollectionStorage storage;
        ThreeMMGroup group;
        ThreeMMPieceFiles files;
        ThreeMMU32 length;
    } ThreeMMPieceTable;
    int three_mm_piece_table_init(ThreeMMPieceTable *, const ThreeMMStorageAllocator *, const ThreeMMPieceFiles *,
                                  int trash_unused);
    int three_mm_piece_table_dispose(ThreeMMPieceTable *);
    /* Returned memory is borrowed until the next mutation. File pieces have no
     * variable bytes. Counts/offsets are decoded native records, never file formats. */
    int three_mm_piece_table_get(const ThreeMMPieceTable *, ThreeMMU32 index, ThreeMMFilePiece *,
                                 const unsigned char **memory);
    int three_mm_piece_table_read(ThreeMMPieceTable *, ThreeMMU32 offset, void *, ThreeMMU32 bytes);
    int three_mm_piece_table_replace(ThreeMMPieceTable *, ThreeMMU32 offset, ThreeMMU32 remove, const void *,
                                     ThreeMMU32 bytes);
    int three_mm_piece_table_replace_file(ThreeMMPieceTable *, ThreeMMU32 offset, ThreeMMU32 remove,
                                          const ThreeMMFilePiece *, int copy);
    int three_mm_piece_table_replace_table(ThreeMMPieceTable *, ThreeMMU32 offset, ThreeMMU32 remove,
                                           ThreeMMPieceTable *source, ThreeMMU32 source_offset, ThreeMMU32 bytes);
    int three_mm_piece_table_compact(ThreeMMPieceTable *);
    /* Write through the legacy per-piece destination windows, restoring its
     * original view and both storage locks on every exit. Preflight rejects
     * the old partial-final-piece overshoot before writing any bytes. */
    int three_mm_piece_table_write(ThreeMMPieceTable *, ThreeMMU32 source_offset, ThreeMMOwnedBlock *destination,
                                   const ThreeMMStorageAllocator *);
#ifdef __cplusplus
}
#endif
#endif
