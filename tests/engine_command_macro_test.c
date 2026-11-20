#include "mm/command_macro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "command macro check failed at line %d\n", line);
        exit(EXIT_FAILURE);
    }
}
#define CHECK(condition) check((condition), __LINE__)

typedef struct Fixture
{
    MMCommandMacro macro;
    unsigned char wire[256];
    MMU32 length;
    int target, group, groups[3];
    int file_refs, has_macro, commits, erased, erased_groups, notifications;
    int done_error, done_mode, fail_group_store, fail_rows_write, fail_commit, system;
    MMU32 done_number;
} Fixture;

static int file_ref(void *context, void *file, int acquire)
{
    Fixture *f = (Fixture *)context;
    CHECK(file == f && f->file_refs == (acquire ? 0 : 1));
    f->file_refs = acquire != 0;
    return MM_MACRO_OK;
}

static int create(void *context, void *file, MMU32 *number)
{
    Fixture *f = (Fixture *)context;
    CHECK(file == f && f->file_refs == 1);
    *number = 7;
    f->has_macro = 1;
    f->length = 0;
    memset(f->groups, 0, sizeof(f->groups));
    return MM_MACRO_OK;
}

static int read_rows(void *context, void *file, MMU32 number, MMCommandMacro *macro)
{
    Fixture *f = (Fixture *)context;
    MMMemoryStream reader;
    CHECK(file == f && f->file_refs == 1 && number == 7);
    if (!f->has_macro)
        return MM_MACRO_FILE_ERROR;
    CHECK(mm_memory_stream_init(&reader, f->wire, f->length));
    return mm_command_macro_read(macro, &reader.stream, f->system);
}

static int write_bytes(void *context, MMU32 offset, const void *bytes, size_t length)
{
    Fixture *f = (Fixture *)context;
    CHECK(offset <= f->length && length <= f->length - offset);
    if (f->fail_rows_write)
        return 0;
    memcpy(f->wire + offset, bytes, length);
    return 1;
}

static int write_rows(void *context, void *file, MMU32 number, MMCommandMacro *macro)
{
    Fixture *f = (Fixture *)context;
    MMStorageWriter writer;
    CHECK(file == f && number == 7 && f->has_macro);
    f->length = mm_command_macro_wire_size(macro);
    CHECK(f->length <= sizeof(f->wire));
    writer.context = context;
    writer.length = f->length;
    writer.write_at = write_bytes;
    return mm_command_macro_write(macro, &writer, f->system);
}

static int erase(void *context, void *file, MMU32 number)
{
    Fixture *f = (Fixture *)context;
    CHECK(file == f && number == 7 && f->has_macro);
    f->has_macro = 0;
    ++f->erased;
    return MM_MACRO_OK;
}

static int commit(void *context, void *file)
{
    Fixture *f = (Fixture *)context;
    CHECK(file == f && f->has_macro);
    ++f->commits;
    return f->fail_commit ? MM_MACRO_FILE_ERROR : MM_MACRO_OK;
}

static int store_group(void *context, void *file, MMU32 number, MMU32 slot, void *group, MMU32 *child)
{
    Fixture *f = (Fixture *)context;
    CHECK(file == f && number == 7 && slot > 0 && slot < 3 && group == &f->group);
    CHECK(!f->groups[slot]);
    *child = 10 + slot;
    f->groups[slot] = 1;
    return f->fail_group_store ? MM_MACRO_FILE_ERROR : MM_MACRO_OK;
}

static int erase_group(void *context, void *file, MMU32 number, MMU32 slot, MMU32 child)
{
    Fixture *f = (Fixture *)context;
    CHECK(file == f && number == 7 && slot > 0 && slot < 3 && child == 10 + slot);
    CHECK(f->groups[slot]);
    f->groups[slot] = 0;
    ++f->erased_groups;
    return MM_MACRO_OK;
}

static int read_group(void *context, void *file, MMU32 number, MMU32 slot, void **group)
{
    Fixture *f = (Fixture *)context;
    CHECK(file == f && number == 7 && slot > 0 && slot < 3);
    if (!f->groups[slot])
        return MM_MACRO_FILE_ERROR;
    *group = &f->group;
    return MM_MACRO_OK;
}

static int lookup(void *context, MMI32 handler_id, void **target)
{
    Fixture *f = (Fixture *)context;
    CHECK(handler_id == 843);
    *target = &f->target;
    return MM_MACRO_OK;
}

static int done(void *context, int mode, int error, MMU32 number)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->macro.mode == (mode == MM_MACRO_RECORDING ? MM_MACRO_IDLE : MM_MACRO_PLAYING));
    CHECK(f->file_refs == (mode == MM_MACRO_PLAYING));
    f->done_mode = mode;
    f->done_error = error;
    f->done_number = number;
    ++f->notifications;
    return MM_MACRO_OK;
}

static const MMCommandMacroRules rules = {113, 114, 400, 600, 200, 400};

typedef struct InputProbe
{
    MMQueuedCommand current;
    char trace[16];
    int count, track_result, play_result, modal_ok;
} InputProbe;

static void trace_input(InputProbe *probe, char step)
{
    CHECK(probe->count < (int)sizeof(probe->trace) - 1);
    probe->trace[probe->count++] = step;
    probe->trace[probe->count] = 0;
}

static void publish_input(void *context, const MMQueuedCommand *command)
{
    InputProbe *probe = (InputProbe *)context;
    static const MMQueuedCommand empty = {0};
    trace_input(probe, 'P');
    probe->current = command ? *command : empty;
}

static int track_input(void *context)
{
    InputProbe *probe = (InputProbe *)context;
    trace_input(probe, 'T');
    probe->current.id = 22;
    return probe->track_result;
}

static MMI32 input_id(void *context)
{
    return ((InputProbe *)context)->current.id;
}

static int discard_input(void *context)
{
    InputProbe *probe = (InputProbe *)context;
    trace_input(probe, 'D');
    probe->current.group = NULL;
    return 0;
}

static int replay_input(void *context)
{
    InputProbe *probe = (InputProbe *)context;
    trace_input(probe, 'R');
    CHECK(!probe->current.group);
    probe->current.id = 23;
    return probe->play_result;
}

static int allow_input(void *context)
{
    InputProbe *probe = (InputProbe *)context;
    trace_input(probe, 'A');
    return probe->modal_ok;
}

static int reject_input(void *context)
{
    InputProbe *probe = (InputProbe *)context;
    trace_input(probe, 'N');
    CHECK(probe->current.group != NULL);
    return 0;
}

static void test_admission(void)
{
    InputProbe probe = {0};
    MMCommandDispatch dispatch;
    MMCommandMacro macro;
    MMQueuedCommand command = {0}, key;
    MMCommandAdmission services = {0, publish_input, 0, input_id, discard_input, replay_input, allow_input,
                                   reject_input};
    services.context = &probe;
    CHECK(mm_commands_init(&dispatch, NULL, 1));
    CHECK(mm_command_macro_init(&macro, NULL, 1));
    CHECK(mm_command_admit_next(&dispatch, &macro, &rules, &services) == MM_COMMAND_INPUT_EMPTY);
    CHECK(!strcmp(probe.trace, "P") && !probe.current.id);
    probe.count = 0;
    services.track = track_input;
    probe.track_result = MM_COMMAND_INPUT_FILTER;
    CHECK(mm_command_admit_next(&dispatch, &macro, &rules, &services) == MM_COMMAND_INPUT_FILTER);
    CHECK(!strcmp(probe.trace, "PT") && probe.current.id == 22);

    probe.count = 0;
    command.id = 24;
    command.group = &probe;
    CHECK(mm_commands_insert(&dispatch, &command, 0));
    CHECK(mm_command_admit_next(&dispatch, &macro, &rules, &services) == MM_COMMAND_INPUT_FILTER);
    CHECK(!strcmp(probe.trace, "PAND") && !probe.current.group && probe.current.id == 24);

    probe.count = 0;
    probe.modal_ok = probe.play_result = 1;
    macro.mode = MM_MACRO_PLAYING;
    CHECK(mm_commands_insert(&dispatch, &command, 0));
    CHECK(mm_command_admit_key(&dispatch, &macro, 24, &key) == -1);
    CHECK(mm_command_admit_next(&dispatch, &macro, &rules, &services) == MM_COMMAND_INPUT_READY);
    CHECK(!strcmp(probe.trace, "PDRA") && probe.current.id == 23);

    probe.count = 0;
    probe.play_result = 0;
    CHECK(mm_commands_insert(&dispatch, &command, 0));
    CHECK(mm_command_admit_next(&dispatch, &macro, &rules, &services) == MM_COMMAND_INPUT_FILTER);
    CHECK(!strcmp(probe.trace, "PDR") && !probe.current.group);

    probe.count = 0;
    command.id = rules.stop_play;
    CHECK(mm_commands_insert(&dispatch, &command, 0));
    CHECK(mm_command_admit_next(&dispatch, &macro, &rules, &services) == MM_COMMAND_INPUT_READY);
    CHECK(!strcmp(probe.trace, "PDA") && probe.current.id == rules.stop_play && !probe.current.group);

    macro.mode = MM_MACRO_IDLE;
    CHECK(mm_command_admit_key(&dispatch, &macro, command.id, &key) == 0);
    command.group = NULL;
    CHECK(mm_commands_insert(&dispatch, &command, 0));
    CHECK(mm_command_admit_key(&dispatch, &macro, 24, &key) == -1);
    CHECK(mm_command_admit_key(&dispatch, &macro, command.id, &key) == 1 && key.id == command.id);
    CHECK(mm_commands_dispose(&dispatch, NULL, NULL) == 0);
}

static void test_replay(Fixture *f, const MMCommandMacroServices *s)
{
    MMQueuedCommand input = {0}, output = {0};
    input.id = 123;
    input.target = &f->target;
    input.arguments[0] = -4;
    CHECK(mm_command_macro_begin_record(&f->macro, s, f) == 1);
    CHECK(mm_command_macro_begin_play(&f->macro, s, f, 7) == 0);
    CHECK(mm_command_macro_after(&f->macro, &rules, 0) == MM_MACRO_IGNORE);
    CHECK(mm_command_macro_after(&f->macro, &rules, 113) == MM_MACRO_FINISH_RECORD);
    CHECK(mm_command_macro_after(&f->macro, &rules, 400) == MM_MACRO_IGNORE);
    CHECK(mm_command_macro_after(&f->macro, &rules, 600) == MM_MACRO_CAPTURE);
    CHECK(mm_command_macro_record(&f->macro, s, &rules, &input, 843) == 1);
    CHECK(mm_command_macro_record(&f->macro, s, &rules, &input, 843) == 1);
    input.id = 200;
    CHECK(mm_command_macro_record(&f->macro, s, &rules, &input, 843) == 1);
    CHECK(mm_command_macro_record(&f->macro, s, &rules, &input, 843) == 1);
    input.id = 124;
    input.group = &f->group;
    CHECK(mm_command_macro_record(&f->macro, s, &rules, &input, 843) == 1);
    CHECK(mm_command_macro_record(&f->macro, s, &rules, &input, 843) == 1);
    CHECK(mm_command_macro_stop_record(&f->macro, s) == 1);
    CHECK(f->length == 12 + 4 * 32 && f->commits == 1 && f->groups[1] && f->groups[2]);
    CHECK(f->done_mode == MM_MACRO_RECORDING && f->done_error == MM_MACRO_OK && f->done_number == 7);

    CHECK(mm_command_macro_begin_play(&f->macro, s, f, 7) == 1);
    CHECK(mm_command_macro_replaces(&f->macro, &rules, 123) && !mm_command_macro_replaces(&f->macro, &rules, 114));
    CHECK(mm_command_macro_after(&f->macro, &rules, 114) == MM_MACRO_FINISH_PLAY);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1);
    CHECK(output.id == 123 && output.arguments[0] == -4 && output.target == &f->target && !output.group);
    mm_command_macro_bury(&f->macro, &f->target);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 200);
    CHECK(mm_command_macro_stop_play(&f->macro, s) == 1 && f->done_error == MM_MACRO_ABORTED);

    CHECK(mm_command_macro_begin_play(&f->macro, s, f, 7) == 1);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 123);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 123);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 200);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 124 && output.group == &f->group);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 124 && output.group == &f->group);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 0);
    CHECK(f->done_mode == MM_MACRO_PLAYING && f->done_error == MM_MACRO_OK && f->file_refs == 0);
}

static void test_failures(Fixture *f, const MMCommandMacroServices *s)
{
    MMQueuedCommand input = {0}, output = {0};
    unsigned big = f->wire[0] == 0;
    unsigned grouped_repeat = 12 + 2 * 32 + 8 + (big ? 3 : 0);
    int notifications = f->notifications;
    f->wire[grouped_repeat] = 2;
    CHECK(mm_command_macro_begin_play(&f->macro, s, f, 7) == 1);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 123);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 123);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 1 && output.id == 200);
    CHECK(mm_command_macro_next(&f->macro, s, &output) == 0 && output.id == 124 && !output.group);
    CHECK(f->done_error == MM_MACRO_FILE_ERROR && f->file_refs == 0);
    f->wire[grouped_repeat] = 1;

    ++f->system;
    CHECK(mm_command_macro_begin_play(&f->macro, s, f, 7) == 1);
    CHECK(f->done_error == MM_MACRO_WRONG_PLATFORM && f->macro.mode == MM_MACRO_IDLE);
    --f->system;
    f->wire[big ? 7 : 4] = 16;
    f->wire[big ? 11 : 8] = 8; /* Valid GL length with an incompatible command row width. */
    CHECK(mm_command_macro_begin_play(&f->macro, s, f, 7) == 1);
    CHECK(f->done_error == MM_MACRO_FILE_ERROR && f->macro.mode == MM_MACRO_IDLE);

    input.id = 123;
    input.group = &f->group;
    f->fail_group_store = 1;
    CHECK(mm_command_macro_begin_record(&f->macro, s, f) == 1);
    CHECK(mm_command_macro_record(&f->macro, s, &rules, &input, 843) == 1);
    CHECK(f->erased_groups == 1 && f->macro.rows.count == 0);
    CHECK(mm_command_macro_stop_record(&f->macro, s) == 1);
    CHECK(f->erased == 1 && !f->has_macro && f->done_error == MM_MACRO_FILE_ERROR && f->done_number == MM_MACRO_NO_CHUNK);

    f->fail_rows_write = 1;
    CHECK(mm_command_macro_begin_record(&f->macro, s, f) == 1);
    CHECK(mm_command_macro_stop_record(&f->macro, s) == 1);
    CHECK(f->erased == 2 && f->commits == 1 && f->done_number == MM_MACRO_NO_CHUNK);
    f->fail_rows_write = 0;
    f->fail_commit = 1;
    CHECK(mm_command_macro_begin_record(&f->macro, s, f) == 1);
    CHECK(mm_command_macro_stop_record(&f->macro, s) == 1);
    CHECK(f->erased == 2 && f->has_macro && f->commits == 2 && f->done_error == MM_MACRO_FILE_ERROR);
    CHECK(f->file_refs == 0 && f->notifications == notifications + 6);
}

int main(void)
{
    Fixture fixture = {0};
    MMCommandMacroServices services = {0, file_ref, create, read_rows, write_rows, erase, commit,
                                      store_group, erase_group, read_group, lookup, done};
    services.context = &fixture;
    fixture.system = 0x0303;
    test_admission();
    CHECK(mm_command_macro_init(&fixture.macro, 0, 1));
    test_replay(&fixture, &services);
    test_failures(&fixture, &services);
    CHECK(mm_command_macro_dispose(&fixture.macro, &services) == MM_MACRO_OK);
    return EXIT_SUCCESS;
}
