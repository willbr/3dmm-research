/* Command macro policy converted from Kauai cmd_core.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_COMMAND_MACRO_H
#define MM_ENGINE_COMMAND_MACRO_H
#include "command_dispatch.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { MM_MACRO_IDLE, MM_MACRO_RECORDING, MM_MACRO_PLAYING };
enum { MM_MACRO_OK, MM_MACRO_FILE_ERROR, MM_MACRO_MEMORY_ERROR, MM_MACRO_WRONG_PLATFORM, MM_MACRO_ABORTED };
#define MM_MACRO_NO_CHUNK ((MMU32)-1)

typedef struct MMCommandMacro
{
    MMCollectionStorage storage;
    MMArray rows;
    void *file;
    MMQueuedCommand previous;
    MMU32 number, cursor, last_group;
    MMI32 repeats;
    int mode, error, previous_had_group;
} MMCommandMacro;

/* I/O callbacks return MM_MACRO_OK, an MM_MACRO error, or a negative host
 * error. read_rows/write_rows wrap the native archive block with the stream
 * API below. A successful group read transfers one reference. Storing a
 * group borrows it and publishes the child number once it has been added,
 * even when writing later fails. Targets are borrowed throughout. */
typedef struct MMCommandMacroServices
{
    void *context;
    int (*file_ref)(void *, void *file, int acquire);
    int (*create)(void *, void *file, MMU32 *number);
    int (*read_rows)(void *, void *file, MMU32 number, MMCommandMacro *);
    int (*write_rows)(void *, void *file, MMU32 number, MMCommandMacro *);
    int (*erase)(void *, void *file, MMU32 number);
    int (*commit)(void *, void *file);
    int (*store_group)(void *, void *file, MMU32 number, MMU32 slot, void *group, MMU32 *child);
    int (*erase_group)(void *, void *file, MMU32 number, MMU32 slot, MMU32 child);
    int (*read_group)(void *, void *file, MMU32 number, MMU32 slot, void **group);
    int (*lookup)(void *, MMI32 handler_id, void **target);
    int (*done)(void *, int mode, int error, MMU32 number);
} MMCommandMacroServices;

typedef struct MMCommandMacroRules
{
    MMI32 stop_record, stop_play, no_record_first, no_record_end, no_repeat_first, no_repeat_end;
} MMCommandMacroRules;

enum { MM_MACRO_IGNORE, MM_MACRO_CAPTURE, MM_MACRO_FINISH_RECORD, MM_MACRO_FINISH_PLAY };

/* Stable-address owner of the retained file and row storage. Begin, record,
 * stop and next return 1 for an accepted operation, 0 when unavailable, or a
 * negative host error. An accepted begin/record may register a format,
 * allocation or file error for the completion notification. Next transfers
 * an owned group on success and may publish header fields before a failed
 * group read, as in Kauai. Dispose returns MM_MACRO_OK or the release error. */
int mm_command_macro_init(MMCommandMacro *, const MMStorageAllocator *, int trash_unused);
int mm_command_macro_dispose(MMCommandMacro *, const MMCommandMacroServices *);
int mm_command_macro_begin_record(MMCommandMacro *, const MMCommandMacroServices *, void *file);
int mm_command_macro_record(MMCommandMacro *, const MMCommandMacroServices *, const MMCommandMacroRules *,
                            const MMQueuedCommand *, MMI32 handler_id);
int mm_command_macro_stop_record(MMCommandMacro *, const MMCommandMacroServices *);
int mm_command_macro_begin_play(MMCommandMacro *, const MMCommandMacroServices *, void *file, MMU32 number);
int mm_command_macro_next(MMCommandMacro *, const MMCommandMacroServices *, MMQueuedCommand *);
int mm_command_macro_stop_play(MMCommandMacro *, const MMCommandMacroServices *);
void mm_command_macro_bury(MMCommandMacro *, const void *target);
int mm_command_macro_replaces(const MMCommandMacro *, const MMCommandMacroRules *, MMI32 id);
int mm_command_macro_after(const MMCommandMacro *, const MMCommandMacroRules *, MMI32 id);

/* GLCM payload. The GL header supplies count, row width, byte order and
 * system. Replay accepts the local byte order and the supplied system code.
 * Rows use four 32-bit arguments and a separate optional GG child slot. */
MMU32 mm_command_macro_wire_size(const MMCommandMacro *);
int mm_command_macro_write(MMCommandMacro *, const MMStorageWriter *, int system);
int mm_command_macro_read(MMCommandMacro *, const MMReadStream *, int system);

#ifdef __cplusplus
}
#endif
#endif
