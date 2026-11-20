/* Command macro policy converted from Kauai cmd_core.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/command_macro.h"
#include <string.h>

typedef struct MMMacroRow
{
    MMI32 id, handler_id, repeats;
    MMU32 group;
    MMI32 arguments[MM_COMMAND_ARGUMENTS];
} MMMacroRow;

typedef char MMMacroRowSize[(sizeof(MMMacroRow) == 32) ? 1 : -1];

int mm_command_macro_init(MMCommandMacro *macro, const MMStorageAllocator *allocator, int trash)
{
    memset(macro, 0, sizeof(*macro));
    if (mm_storage_init(&macro->storage, allocator) != MM_STORAGE_OK)
        return 0;
    macro->storage.min_grow[0] = 128;
    macro->storage.min_grow[1] = 16;
    macro->rows.storage = &macro->storage;
    macro->rows.width = sizeof(MMMacroRow);
    macro->rows.free_count = -1;
    macro->rows.trash_unused = trash;
    macro->number = MM_MACRO_NO_CHUNK;
    return 1;
}

static void clear_rows(MMCommandMacro *macro)
{
    mm_storage_dispose(&macro->storage);
    macro->rows.count = 0;
    macro->previous.target = 0;
    macro->repeats = 0;
}

static int release_file(MMCommandMacro *macro, const MMCommandMacroServices *s)
{
    void *file = macro->file;
    macro->file = 0;
    return file ? s->file_ref(s->context, file, 0) : MM_MACRO_OK;
}

int mm_command_macro_dispose(MMCommandMacro *macro, const MMCommandMacroServices *s)
{
    int status;
    macro->mode = MM_MACRO_IDLE;
    status = release_file(macro, s);
    clear_rows(macro);
    return status;
}

static int begin(MMCommandMacro *macro, const MMCommandMacroServices *s, void *file, int mode, MMU32 number)
{
    int status;
    if (macro->mode != MM_MACRO_IDLE || !file)
        return 0;
    status = s->file_ref(s->context, file, 1);
    if (status != MM_MACRO_OK)
        return status < 0 ? status : 0;
    macro->file = file;
    macro->mode = mode;
    macro->error = MM_MACRO_OK;
    macro->number = number;
    macro->cursor = macro->last_group = 0;
    macro->repeats = 0;
    return 1;
}

int mm_command_macro_begin_record(MMCommandMacro *macro, const MMCommandMacroServices *s, void *file)
{
    MMU32 number = MM_MACRO_NO_CHUNK;
    int status = begin(macro, s, file, MM_MACRO_RECORDING, MM_MACRO_NO_CHUNK);
    if (status <= 0)
        return status;
    if (mm_array_reserve(&macro->rows, 100, 0) != MM_STORAGE_OK)
    {
        macro->error = MM_MACRO_MEMORY_ERROR;
        return 1;
    }
    status = s->create(s->context, file, &number);
    if (status == MM_MACRO_OK)
        macro->number = number;
    else
        macro->error = MM_MACRO_FILE_ERROR;
    return status < 0 ? status : 1;
}

static void finalize_repeat(MMCommandMacro *macro)
{
    MMMacroRow row;
    unsigned char *entry;
    if (macro->repeats > 1)
    {
        entry = macro->storage.buffers[0].data + macro->cursor * sizeof(row);
        memcpy(&row, entry, sizeof(row));
        row.repeats = macro->repeats;
        memcpy(entry, &row, sizeof(row));
    }
}

int mm_command_macro_record(MMCommandMacro *macro, const MMCommandMacroServices *s,
                            const MMCommandMacroRules *rules, const MMQueuedCommand *command,
                            MMI32 handler_id)
{
    MMMacroRow row;
    MMU32 child = MM_MACRO_NO_CHUNK;
    int status, undo;
    if (macro->mode != MM_MACRO_RECORDING || macro->error != MM_MACRO_OK)
        return 0;

    if (macro->repeats > 0)
    {
        if (!macro->previous_had_group && !command->group && command->id == macro->previous.id &&
            command->target == macro->previous.target &&
            memcmp(command->arguments, macro->previous.arguments, sizeof(row.arguments)) == 0)
        {
            if ((command->id < rules->no_repeat_first || command->id >= rules->no_repeat_end) &&
                macro->repeats < 0x7fffffffL)
                ++macro->repeats;
            return 1;
        }
        finalize_repeat(macro);
        ++macro->cursor;
        macro->repeats = 0;
    }

    row.id = command->id;
    row.handler_id = handler_id;
    row.repeats = 1;
    row.group = command->group ? ++macro->last_group : 0;
    memcpy(row.arguments, command->arguments, sizeof(row.arguments));
    if (mm_array_insert(&macro->rows, macro->cursor, &row, 1) != MM_STORAGE_OK)
    {
        macro->error = MM_MACRO_MEMORY_ERROR;
        return 1;
    }
    if (row.group)
    {
        status = s->store_group(s->context, macro->file, macro->number, row.group, command->group, &child);
        if (status != MM_MACRO_OK)
        {
            undo = child == MM_MACRO_NO_CHUNK ? MM_MACRO_OK :
                s->erase_group(s->context, macro->file, macro->number, row.group, child);
            mm_array_delete(&macro->rows, macro->cursor, 1);
            macro->error = MM_MACRO_FILE_ERROR;
            return status < 0 ? status : undo < 0 ? undo : 1;
        }
    }
    macro->repeats = 1;
    macro->previous = *command;
    macro->previous.group = 0;
    macro->previous_had_group = command->group != 0;
    return 1;
}

int mm_command_macro_stop_record(MMCommandMacro *macro, const MMCommandMacroServices *s)
{
    int result = 1, status;
    if (macro->mode != MM_MACRO_RECORDING)
        return 0;
    if (macro->error == MM_MACRO_OK)
    {
        finalize_repeat(macro);
        status = s->write_rows(s->context, macro->file, macro->number, macro);
        if (status != MM_MACRO_OK)
            macro->error = MM_MACRO_FILE_ERROR;
        if (status < 0)
            result = status;
    }
    if (macro->error != MM_MACRO_OK && macro->number != MM_MACRO_NO_CHUNK)
    {
        status = s->erase(s->context, macro->file, macro->number);
        if (result > 0 && status < 0)
            result = status;
        macro->number = MM_MACRO_NO_CHUNK;
    }
    if (macro->number != MM_MACRO_NO_CHUNK)
    {
        status = s->commit(s->context, macro->file);
        if (status != MM_MACRO_OK)
        {
            macro->error = MM_MACRO_FILE_ERROR;
            macro->number = MM_MACRO_NO_CHUNK;
        }
        if (status < 0)
            result = status;
    }
    macro->mode = MM_MACRO_IDLE;
    clear_rows(macro);
    status = release_file(macro, s);
    if (result > 0 && status < 0)
        result = status;
    status = s->done(s->context, MM_MACRO_RECORDING, macro->error, macro->number);
    return result > 0 && status < 0 ? status : result;
}

int mm_command_macro_stop_play(MMCommandMacro *macro, const MMCommandMacroServices *s)
{
    int notified, released;
    if (macro->mode != MM_MACRO_PLAYING)
        return 0;
    if (macro->error == MM_MACRO_OK && (macro->repeats > 0 || macro->cursor < macro->rows.count))
        macro->error = MM_MACRO_ABORTED;
    notified = s->done(s->context, MM_MACRO_PLAYING, macro->error, macro->number);
    macro->mode = MM_MACRO_IDLE;
    released = release_file(macro, s);
    clear_rows(macro);
    if (notified < 0)
        return notified;
    return released < 0 ? released : 1;
}

int mm_command_macro_begin_play(MMCommandMacro *macro, const MMCommandMacroServices *s, void *file, MMU32 number)
{
    int stopped, status = begin(macro, s, file, MM_MACRO_PLAYING, number);
    if (status <= 0)
        return status;
    status = s->read_rows(s->context, file, number, macro);
    if (status == MM_MACRO_OK)
        return 1;
    macro->error = status < 0 ? MM_MACRO_FILE_ERROR : status;
    stopped = mm_command_macro_stop_play(macro, s);
    return status < 0 ? status : stopped;
}

int mm_command_macro_next(MMCommandMacro *macro, const MMCommandMacroServices *s, MMQueuedCommand *command)
{
    MMMacroRow row;
    int stopped, status = MM_MACRO_OK;
    if (macro->mode != MM_MACRO_PLAYING)
        return 0;
    if (macro->repeats > 0)
    {
        *command = macro->previous;
        --macro->repeats;
        return 1;
    }
    if (macro->cursor < macro->rows.count)
    {
        memcpy(&row, macro->storage.buffers[0].data + macro->cursor * sizeof(row), sizeof(row));
        command->id = row.id;
        command->target = command->group = 0;
        memcpy(command->arguments, row.arguments, sizeof(row.arguments));
        status = s->lookup(s->context, row.handler_id, &command->target);
        if (status == MM_MACRO_OK && row.group)
        {
            status = row.repeats > 1 ? MM_MACRO_FILE_ERROR :
                s->read_group(s->context, macro->file, macro->number, row.group, &command->group);
        }
        if (status == MM_MACRO_OK)
        {
            ++macro->cursor;
            macro->repeats = row.repeats > 1 ? row.repeats - 1 : 0;
            if (macro->repeats)
                macro->previous = *command;
            return 1;
        }
    }
    macro->error = status < 0 ? MM_MACRO_FILE_ERROR : status;
    stopped = mm_command_macro_stop_play(macro, s);
    return status < 0 ? status : stopped < 0 ? stopped : 0;
}

void mm_command_macro_bury(MMCommandMacro *macro, const void *target)
{
    if (macro->previous.target == target)
    {
        macro->previous.target = 0;
        macro->repeats = 0;
    }
}

int mm_command_macro_replaces(const MMCommandMacro *macro, const MMCommandMacroRules *rules, MMI32 id)
{
    return macro->mode == MM_MACRO_PLAYING && id != rules->stop_play;
}

int mm_command_macro_after(const MMCommandMacro *macro, const MMCommandMacroRules *rules, MMI32 id)
{
    if (macro->mode == MM_MACRO_RECORDING)
    {
        if (id == rules->stop_record)
            return MM_MACRO_FINISH_RECORD;
        if (id && (id < rules->no_record_first || id >= rules->no_record_end))
            return MM_MACRO_CAPTURE;
    }
    else if (macro->mode == MM_MACRO_PLAYING && id == rules->stop_play)
        return MM_MACRO_FINISH_PLAY;
    return MM_MACRO_IGNORE;
}

int mm_command_admit_next(MMCommandDispatch *dispatch, const MMCommandMacro *macro,
                          const MMCommandMacroRules *rules, const MMCommandAdmission *s)
{
    MMQueuedCommand next;
    int status;
    if (mm_commands_take(dispatch, &next))
        s->publish(s->context, &next);
    else
    {
        s->publish(s->context, NULL);
        if (!s->track)
            return MM_COMMAND_INPUT_EMPTY;
        status = s->track(s->context);
        if (status != MM_COMMAND_INPUT_READY)
            return status;
    }

    if (macro->mode == MM_MACRO_PLAYING)
    {
        status = s->discard_group(s->context);
        if (status < 0)
            return status;
        if (mm_command_macro_replaces(macro, rules, s->id(s->context)))
        {
            status = s->replay(s->context);
            if (status <= 0)
                return status < 0 ? status : MM_COMMAND_INPUT_FILTER;
        }
    }

    status = s->allowed(s->context);
    if (status)
        return status < 0 ? status : MM_COMMAND_INPUT_READY;
    status = s->reject(s->context);
    if (status < 0)
        return status;
    status = s->discard_group(s->context);
    return status < 0 ? status : MM_COMMAND_INPUT_FILTER;
}

int mm_command_admit_key(MMCommandDispatch *dispatch, const MMCommandMacro *macro, MMI32 key_id,
                         MMQueuedCommand *key)
{
    if (macro->mode != MM_MACRO_IDLE)
        return -1;
    return mm_commands_take_key(dispatch, key_id, key);
}

static int native_big_endian(void)
{
    const unsigned short one = 1;
    return *(const unsigned char *)&one == 0;
}

MMU32 mm_command_macro_wire_size(const MMCommandMacro *macro)
{
    return 12 + macro->rows.count * sizeof(MMMacroRow);
}

int mm_command_macro_write(MMCommandMacro *macro, const MMStorageWriter *writer, int system)
{
    MMCollectionHeader header = {0};
    unsigned char wire[12];
    header.kind = MM_COLLECTION_ARRAY;
    header.width = sizeof(MMMacroRow);
    header.count = macro->rows.count;
    header.big_endian = native_big_endian();
    header.system = system;
    if (mm_collection_header_write(&header, wire, sizeof(wire)) != MM_STORAGE_OK)
        return MM_MACRO_FILE_ERROR;
    return mm_storage_write(&macro->storage, writer, wire, sizeof(wire),
                            macro->rows.count * sizeof(MMMacroRow), 0) == MM_STORAGE_OK ?
        MM_MACRO_OK : MM_MACRO_FILE_ERROR;
}

int mm_command_macro_read(MMCommandMacro *macro, const MMReadStream *reader, int system)
{
    MMCollectionHeader header;
    if (macro->rows.count ||
        mm_collection_header_read(reader, MM_COLLECTION_ARRAY, 0, &header) != MM_STORAGE_OK ||
        header.width != sizeof(MMMacroRow))
        return MM_MACRO_FILE_ERROR;
    if (header.big_endian != native_big_endian() || header.system != system)
        return MM_MACRO_WRONG_PLATFORM;
    if (mm_storage_read(&macro->storage, reader, header.header_bytes, header.first_bytes, 0) != MM_STORAGE_OK)
        return MM_MACRO_FILE_ERROR;
    macro->rows.count = header.count;
    return MM_MACRO_OK;
}
