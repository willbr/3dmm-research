/* Piece-table policy converted from Kauai stream.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_PIECE_TABLE_H
#define MM_ENGINE_PIECE_TABLE_H
#include "collections.h"
#include "data_stream.h"
#include "block_owner.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMFilePiece
    {
        void *file;
        MMU32 offset, length;
    } MMFilePiece;
    /* File identities are borrowed on input; each stored file piece owns a
     * reference. create_temp returns an owned reference. Copies between tables
     * require the same file ownership domain. Callbacks must not edit/re-enter
     * either table. read/write/copy are exact, positional operations. */
    typedef struct MMPieceFiles
    {
        void *context;
        void (*retain)(void *, void *file);
        void (*release)(void *, void *file);
        void *(*create_temp)(void *);
        int (*read)(void *, const MMFilePiece *, void *output);
        int (*write)(void *, const MMFilePiece *, const void *input);
        int (*copy)(void *, const MMFilePiece *, const MMFilePiece *);
    } MMPieceFiles;
    /* Owns C group storage and file references. Initialize before use; no copying.
     * Insertion inputs must not alias table storage. Failures can leave capacity
     * changes or equivalent splits; logical bytes and file references are retained.
     * Cross-table replacement inserts first and rolls back before deleting on error. */
    typedef struct MMPieceTable
    {
        MMCollectionStorage storage;
        MMGroup group;
        MMPieceFiles files;
        MMU32 length;
    } MMPieceTable;
    int mm_piece_table_init(MMPieceTable *, const MMStorageAllocator *, const MMPieceFiles *,
                                  int trash_unused);
    int mm_piece_table_dispose(MMPieceTable *);
    /* Returned memory is borrowed until the next mutation. File pieces have no
     * variable bytes. Counts/offsets are decoded native records, never file formats. */
    int mm_piece_table_get(const MMPieceTable *, MMU32 index, MMFilePiece *,
                                 const unsigned char **memory);
    int mm_piece_table_read(MMPieceTable *, MMU32 offset, void *, MMU32 bytes);
    int mm_piece_table_replace(MMPieceTable *, MMU32 offset, MMU32 remove, const void *,
                                     MMU32 bytes);
    int mm_piece_table_replace_file(MMPieceTable *, MMU32 offset, MMU32 remove,
                                          const MMFilePiece *, int copy);
    int mm_piece_table_replace_table(MMPieceTable *, MMU32 offset, MMU32 remove,
                                           MMPieceTable *source, MMU32 source_offset, MMU32 bytes);
    int mm_piece_table_compact(MMPieceTable *);
    /* Write through the legacy per-piece destination windows, restoring its
     * original view and both storage locks on every exit. Preflight rejects
     * the old partial-final-piece overshoot before writing any bytes. */
    int mm_piece_table_write(MMPieceTable *, MMU32 source_offset, MMOwnedBlock *destination,
                                   const MMStorageAllocator *);
#ifdef __cplusplus
}
#endif
#endif
