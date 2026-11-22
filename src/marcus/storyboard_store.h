#ifndef MARCUS_STORYBOARD_STORE_H
#define MARCUS_STORYBOARD_STORE_H

#include "mm/archive_reader.h"
#include "mm/movie_scene_journal.h"
#include "mm/private_archive.h"
#include "storyboard_edit_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusBoardData MarcusBoardData;
typedef struct MarcusStoredBoard
{
    MMU32 scene, background;
    MMI32 source;
    MarcusBoardData *data;
} MarcusStoredBoard;

typedef struct MarcusBoardStore
{
    MMCollectionStorage storage;
    MMU32 count;
    const MMMovieEditFileServices *files;
} MarcusBoardStore;

/* Each entry owns one immutable copy of the board and one background archive
 * lease. Copies share that data for undo. Entry pointers last until store
 * mutation. Views last while a snapshot retains their data. A failed set or
 * copy preserves the destination. */
int marcus_board_store_init(MarcusBoardStore *, const MMStorageAllocator *, const MMMovieEditFileServices *);
void marcus_board_store_dispose(MarcusBoardStore *);
int marcus_board_store_copy(MarcusBoardStore *, const MarcusBoardStore *);
int marcus_board_store_set(MarcusBoardStore *, MMU32 scene, MMI32 source, MMU32 background, void *file,
                           const MarcusBoardWire *);
const MarcusStoredBoard *marcus_board_store_data(const MarcusBoardStore *);
const MarcusStoredBoard *marcus_board_store_find(const MarcusBoardStore *, MMU32 scene);
const MarcusBoardWire *marcus_stored_board_view(const MarcusStoredBoard *);
void *marcus_stored_board_file(const MarcusStoredBoard *);

enum
{
    MARCUS_BOARD_READ_LINKS = MARCUS_BOARD_WIRE_VALUES + 1,
    MARCUS_BOARD_READ_PAYLOAD,
    MARCUS_BOARD_READ_STOPPED
};
/* Visit slot-zero storyboard metadata in scene chunk order. The callback
 * borrows each decoded board through the call and must not re-enter the reader.
 * Returns a wire status or a read error. The caller stages any published work. */
typedef int MarcusBoardVisit(void *, MMU32 scene, const MarcusBoardWire *);
int marcus_board_read_archive(MMArchiveReader *, MarcusBoardVisit *, void *context);

/* Copy the generated background and embed current-version editor data. The
 * file being edited is private and the caller discards it on failure. */
const char *marcus_board_embed(MMPrivateArchive *target, MMPrivateArchive *source, const MarcusStoredBoard *);

#ifdef __cplusplus
}
#endif
#endif
