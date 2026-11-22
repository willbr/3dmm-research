/* Kidspace script operations converted from Kauai screxeg.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/script_kidspace.h"
#include "mm/script_opcodes.h"
#include "mm/script_vm.h"
#include "mm/string_format.h"
#include "mm/wide.h"
#include <string.h>

enum { RESIZE, MOVE_RELATIVE, MOVE_ABSOLUTE };

static MMI32 target(MMScriptVM *vm, int explicit_id)
{
    return explicit_id ? mm_script_vm_pop(vm) : vm->this_object;
}

static void *find(MMScriptVM *vm, MMI32 id)
{
    return vm->error ? NULL : vm->kidspace->find(vm->host, id);
}

static void *find_kidspace(MMScriptVM *vm, MMI32 id)
{
    void *object = find(vm, id);
    return object && vm->kidspace->is_kidspace_object(vm->host, object) ? object : NULL;
}

static void notice(MMScriptVM *vm, int reason, unsigned opcode, MMI32 id, MMU32 detail)
{
    if (vm->kidspace->notice)
        vm->kidspace->notice(vm->host, reason, opcode, id, detail);
}

static MMI32 add(MMI32 left, MMI32 right)
{
    return mm_signed_word((MMU32)left + (MMU32)right);
}

static MMI32 subtract(MMI32 left, MMI32 right)
{
    return mm_signed_word((MMU32)left - (MMU32)right);
}

static void change_rect(MMScriptVM *vm, unsigned opcode, int mode, int explicit_id)
{
    const MMKidspaceScriptServices *host = vm->kidspace;
    MMI32 id = target(vm, explicit_id);
    MMI32 x = mm_script_vm_pop(vm);
    MMI32 y = mm_script_vm_pop(vm);
    void *object = find(vm, id);
    MMPixelRect rect;
    MMPixelPoint registration;

    if (!object || !host->rect(vm->host, object, MM_KIDSPACE_MOVABLE_RECT, &rect))
    {
        notice(vm, MM_KIDSPACE_MISSING_OBJECT, opcode, id, 0);
        return;
    }
    if (mode == RESIZE)
    {
        rect.right = add(rect.left, x > 0 ? x : 0);
        rect.bottom = add(rect.top, y > 0 ? y : 0);
    }
    else
    {
        if (mode == MOVE_ABSOLUTE)
        {
            if (host->point(vm->host, object, MM_KIDSPACE_LOCAL_REGISTRATION, &registration))
            {
                x = subtract(x, registration.x);
                y = subtract(y, registration.y);
            }
            x = subtract(x, rect.left);
            y = subtract(y, rect.top);
        }
        rect.left = add(rect.left, x);
        rect.right = add(rect.right, x);
        rect.top = add(rect.top, y);
        rect.bottom = add(rect.bottom, y);
    }
    host->set_rect(vm->host, object, &rect);
}

static void query_point(MMScriptVM *vm, unsigned opcode, int mode, int explicit_id, int y_axis)
{
    const MMKidspaceScriptServices *host = vm->kidspace;
    MMI32 id = target(vm, explicit_id);
    void *object = find(vm, id);
    MMPixelPoint point;
    MMPixelRect rect;
    if (object && host->point(vm->host, object, mode, &point))
        mm_script_vm_push(vm, y_axis ? point.y : point.x);
    else if (mode == MM_KIDSPACE_PARENT_REGISTRATION && object &&
             host->rect(vm->host, object, MM_KIDSPACE_PARENT_RECT, &rect))
        mm_script_vm_push(vm, y_axis ? rect.top : rect.left);
    else
    {
        notice(vm, MM_KIDSPACE_MISSING_OBJECT, opcode, id, 0);
        mm_script_vm_push(vm, 0);
    }
}

static void query_extent(MMScriptVM *vm, unsigned opcode, int explicit_id, int y_axis)
{
    MMI32 id = target(vm, explicit_id);
    void *object = find(vm, id);
    MMPixelRect rect;
    if (object && vm->kidspace->rect(vm->host, object, MM_KIDSPACE_LOCAL_RECT, &rect))
        mm_script_vm_push(vm, y_axis ? subtract(rect.bottom, rect.top) : subtract(rect.right, rect.left));
    else
    {
        notice(vm, MM_KIDSPACE_MISSING_OBJECT, opcode, id, 0);
        mm_script_vm_push(vm, 0);
    }
}

static void query_relative(MMScriptVM *vm, int relation, int explicit_id)
{
    const MMKidspaceScriptServices *host = vm->kidspace;
    void *object = find(vm, target(vm, explicit_id));
    if (object)
        object = host->relative(vm->host, object, relation);
    mm_script_vm_push(vm, object ? host->id(vm->host, object) : 0);
}

static void query_descendent(MMScriptVM *vm)
{
    MMI32 child = mm_script_vm_pop(vm);
    MMI32 parent = mm_script_vm_pop(vm);
    void *ancestor = find(vm, parent);
    void *object = ancestor ? find(vm, child) : NULL;
    while (object && object != ancestor)
        object = vm->kidspace->relative(vm->host, object, MM_KIDSPACE_PARENT);
    mm_script_vm_push(vm, ancestor && object == ancestor);
}

static void enqueue(MMScriptVM *vm)
{
    MMQueuedCommand command;
    MMI32 id;
    unsigned i;
    command.id = mm_script_vm_pop(vm);
    id = mm_script_vm_pop(vm);
    command.target = id && !vm->error ? vm->kidspace->handler(vm->host, id) : NULL;
    command.group = NULL;
    for (i = 0; i < MM_COMMAND_ARGUMENTS; ++i)
        command.arguments[i] = mm_script_vm_pop(vm);
    if (!vm->error)
        vm->kidspace->enqueue(vm->host, &command);
}

static void run_script(MMScriptVM *vm, unsigned opcode)
{
    const int explicit_id = opcode == kopRunScriptGob || opcode == kopRunScriptCnoGob;
    const int by_number = opcode == kopRunScriptCnoThis || opcode == kopRunScriptCnoGob;
    MMI32 count = mm_script_vm_pop(vm);
    MMI32 id = target(vm, explicit_id);
    MMU32 script = (MMU32)mm_script_vm_pop(vm);
    MMI32 result = 0, i;
    MMCollectionStorage parameters;
    unsigned char *output = NULL;
    const unsigned char *input;
    void *object;
    int status;
    if (vm->error)
        return;
    if (mm_storage_init(&parameters, &vm->stack.storage.allocator) != MM_STORAGE_OK)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_RESOURCE);
        return;
    }
    if (count > 0)
    {
        if (!mm_script_vm_words(vm, count))
            return;
        if (mm_storage_resize_exact(&parameters, 0, (MMU32)count * sizeof(MMI32), MM_STORAGE_NORMAL) == MM_STORAGE_OK)
        {
            output = parameters.buffers[0].data;
            input = mm_script_vm_words(vm, count);
            for (i = 0; input && i < count; ++i)
                memcpy(output + i * sizeof(MMI32), input + (count - 1 - i) * sizeof(MMI32), sizeof(MMI32));
        }
        else
            notice(vm, MM_KIDSPACE_ARGUMENT_ALLOCATION, opcode, id, script);
        mm_script_vm_drop(vm, count);
        if (!output)
            count = 0;
    }
    if (!vm->error)
    {
        object = find(vm, id);
        status = object ? vm->kidspace->run(vm->host, object, script, by_number, output, count, &result) : -1;
        if (status != 1)
        {
            notice(vm, status < 0 ? MM_KIDSPACE_MISSING_OBJECT : MM_KIDSPACE_NESTED_FAILURE, opcode, id, script);
            result = 0;
        }
    }
    mm_storage_dispose(&parameters);
    mm_script_vm_push(vm, result);
}

static void play_cell(MMScriptVM *vm, unsigned opcode)
{
    MMU32 child;
    MMI32 x, y, delay;
    void *object;
    if (opcode == kopCell)
        vm->paused = 1;
    child = (MMU32)mm_script_vm_pop(vm);
    x = mm_script_vm_pop(vm);
    y = mm_script_vm_pop(vm);
    delay = mm_script_vm_pop(vm);
    object = find(vm, vm->this_object);
    if (object)
        vm->kidspace->set_rep(vm->host, object, child, MM_KIDSPACE_REP_SKIP_ANIMATION, 0, x, y, (MMU32)delay);
    else
        notice(vm, MM_KIDSPACE_MISSING_OBJECT, opcode, vm->this_object, 0);
}

static void object_lifetime(MMScriptVM *vm, unsigned opcode, int explicit_id)
{
    const MMKidspaceScriptServices *host = vm->kidspace;
    const MMKidspaceObjectServices *objects = host->objects;
    MMI32 id = target(vm, explicit_id);
    MMI32 child_id = 0;
    MMU32 resource = 0;
    void *object, *child;

    if (opcode == kopCreateChildThis)
        child_id = mm_script_vm_pop(vm);
    if (opcode == kopCreateChildThis || opcode == kopCreateHelpThis)
    {
        resource = (MMU32)mm_script_vm_pop(vm);
        object = find(vm, id);
        child = NULL;
        if (object)
        {
            host->forget_current(vm->host);
            if (opcode == kopCreateChildThis)
                child = objects->create_child(vm->host, object, child_id, resource);
            else
                child = objects->create_help(vm->host, object, resource);
        }
        else
            notice(vm, MM_KIDSPACE_MISSING_OBJECT, opcode - explicit_id, id, resource);
        mm_script_vm_push(vm, child ? host->id(vm->host, child) : 0);
        return;
    }

    object = find(vm, id);
    if (!object)
        return;
    host->forget_current(vm->host);
    if (opcode == kopDestroyThis && object != objects->world(vm->host))
    {
        objects->release(vm->host, object);
        return;
    }
    if (opcode == kopDestroyThis)
        notice(vm, MM_KIDSPACE_DESTROY_WORLD, opcode - explicit_id, id, 0);
    while ((child = host->relative(vm->host, object, MM_KIDSPACE_FIRST_CHILD)) != NULL)
        objects->release(vm->host, child);
}

static void object_query(MMScriptVM *vm, unsigned opcode, int explicit_id)
{
    const MMKidspaceObjectServices *objects = vm->kidspace->objects;
    MMI32 id = target(vm, explicit_id);
    void *object = find_kidspace(vm, id);
    MMI32 result = 0;

    if (!object)
        notice(vm, MM_KIDSPACE_MISSING_OBJECT, opcode - explicit_id, id, 0);
    else
    {
        switch (opcode)
        {
        case kopStateThis: result = objects->state(vm->host, object); break;
        case kopZThis: result = objects->z_plane(vm->host, object); break;
        case kopPlayingThis: result = objects->playing(vm->host, object) != 0; break;
        case kopCurFrameThis: result = objects->current_frame(vm->host, object); break;
        case kopCountFramesThis: result = objects->frame_count(vm->host, object); break;
        }
    }
    mm_script_vm_push(vm, result);
}

static void object_control(MMScriptVM *vm, unsigned opcode, int explicit_id)
{
    const MMKidspaceScriptServices *host = vm->kidspace;
    const MMKidspaceObjectServices *objects = host->objects;
    MMI32 id = target(vm, explicit_id);
    MMI32 first = 0, filter_id = 0;
    MMU32 filter_script = 0;
    void *object;

    if (opcode != kopPlayThis && opcode != kopStopThis)
        first = mm_script_vm_pop(vm);
    if (opcode == kopFilterCmdsThis)
    {
        filter_id = mm_script_vm_pop(vm);
        filter_script = (MMU32)mm_script_vm_pop(vm);
    }
    object = find_kidspace(vm, id);
    if (!object)
    {
        notice(vm, MM_KIDSPACE_MISSING_OBJECT, opcode - explicit_id, id, 0);
        return;
    }
    switch (opcode)
    {
    case kopChangeStateThis:
        if ((MMU32)first >= MM_KIDSPACE_STATE_LIMIT)
            notice(vm, MM_KIDSPACE_INVALID_STATE, opcode - explicit_id, id, (MMU32)first);
        else
        {
            host->forget_current(vm->host);
            objects->change_state(vm->host, object, first);
        }
        break;
    case kopAnimateThis:
        host->set_rep(vm->host, object, (MMU32)first, 0, MM_KIDSPACE_TAG_ANIMATION, 0, 0, 0);
        break;
    case kopSetPictureThis:
        host->set_rep(vm->host, object, (MMU32)first, MM_KIDSPACE_REP_KILL_ANIMATION, MM_KIDSPACE_TAG_BITMAP, 0, 0, 0);
        break;
    case kopSetRepThis:
        host->set_rep(vm->host, object, (MMU32)first, MM_KIDSPACE_REP_KILL_ANIMATION, 0, 0, 0, 0);
        break;
    case kopSetZThis: objects->set_z_plane(vm->host, object, first); break;
    case kopPlayThis: objects->play(vm->host, object); break;
    case kopStopThis: objects->stop(vm->host, object); break;
    case kopGotoFrameThis: objects->go_to_frame(vm->host, object, first); break;
    case kopFilterCmdsThis:
        if (!objects->filter(vm->host, object, first, filter_id, filter_script))
            notice(vm, MM_KIDSPACE_FILTER_FAILED, opcode - explicit_id, id, 0);
        break;
    case kopSetNoSlipThis: objects->set_no_slip(vm->host, object, first != 0); break;
    case kopSetToolTipSourceThis: objects->set_tooltip_source(vm->host, object, first); break;
    case kopStreamThis: objects->stream(vm->host, object, first != 0); break;
    }
}

static void set_alarm(MMScriptVM *vm, unsigned opcode, const MMKidspaceClockServices *clocks)
{
    MMI32 target_id = target(vm, opcode == kopSetAlarmGob);
    MMI32 clock_id = mm_script_vm_pop(vm);
    MMI32 delay = mm_script_vm_pop(vm);
    MMI32 script = opcode == kopSetAlarm ? -1 : mm_script_vm_pop(vm);
    void *object = find(vm, target_id);
    MMAlarmClock *clock = object ? clocks->find(vm->host, clock_id) : NULL;
    if (!clock || !clocks->alarm(vm->host, clock, object, delay, script))
        notice(vm, MM_KIDSPACE_ALARM_FAILED, opcode, clock_id, 0);
}

static void clock_command(MMScriptVM *vm, unsigned opcode)
{
    const MMKidspaceClockServices *clocks = vm->kidspace->clocks;
    MMAlarmClock *clock;
    MMI32 id;
    MMU32 time = 0;
    if (!clocks)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        return;
    }
    if (opcode == kopSetAlarm || opcode == kopSetAlarmThis || opcode == kopSetAlarmGob)
    {
        set_alarm(vm, opcode, clocks);
        return;
    }
    id = mm_script_vm_pop(vm);
    if (opcode == kopStartClock)
        time = (MMU32)mm_script_vm_pop(vm);
    if (vm->error)
        return;
    clock = id || opcode != kopCreateClock ? clocks->find(vm->host, id) : NULL;
    if (opcode == kopCreateClock)
    {
        if (clock)
        {
            notice(vm, MM_KIDSPACE_CLOCK_RETAINED, opcode, id, 0);
            clocks->retain(vm->host, clock);
        }
        else
        {
            if (!id)
                id = vm->kidspace->unique_id(vm->host);
            if (!clocks->create(vm->host, id))
                id = 0;
        }
        mm_script_vm_push(vm, id);
        return;
    }
    if (!clock && opcode != kopDestroyClock)
        notice(vm, MM_KIDSPACE_CLOCK_MISSING, opcode, id, 0);
    switch (opcode)
    {
    case kopDestroyClock:
        if (clock)
            clocks->release(vm->host, clock);
        break;
    case kopStartClock:
        if (clock)
            clocks->start(vm->host, clock, time);
        break;
    case kopStopClock:
        if (clock)
            clocks->stop(vm->host, clock);
        break;
    case kopTimeCur:
        mm_script_vm_push(vm, clock ? mm_signed_word(mm_alarm_clock_time(clock, 0, 0)) : 0);
        break;
    }
}

static void play_sound(MMScriptVM *vm, unsigned opcode, const MMKidspaceSoundServices *sound)
{
    MMI32 id = target(vm, opcode == kopPlaySoundGob);
    void *object;
    const void *words;
    MMI32 args[7], result;
    MMSoundPlay play = {0};
    if (vm->error)
        return;
    object = id ? find_kidspace(vm, id) : NULL;
    if (id && !object)
        notice(vm, MM_KIDSPACE_SOUND_FALLBACK, opcode, id, 0);
    words = mm_script_vm_words(vm, 7);
    if (!words)
        return;
    memcpy(args, words, sizeof(args));
    play.type = (MMU32)args[6];
    play.number = (MMU32)args[5];
    play.queue = args[4];
    play.volume = args[3];
    play.plays = args[2];
    play.priority = args[1];
    play.sound_class = args[0];
    result = sound->play(vm->host, object, &play);
    mm_script_vm_drop(vm, 7);
    mm_script_vm_push(vm, result);
}

static void play_mouse_sound(MMScriptVM *vm, unsigned opcode, const MMKidspaceSoundServices *sound)
{
    MMI32 id = target(vm, opcode == kopPlayMouseSoundGob);
    MMU32 type = (MMU32)mm_script_vm_pop(vm);
    MMU32 number = (MMU32)mm_script_vm_pop(vm);
    void *object = find_kidspace(vm, id);
    MMI32 result = 0;
    if (object)
        result = sound->play_mouse(vm->host, object, type, number);
    mm_script_vm_push(vm, result);
}

static void sound_command(MMScriptVM *vm, unsigned opcode)
{
    const MMKidspaceSoundServices *sound = vm->kidspace->sound;
    static const int operations[] = {
        MM_SOUND_INSTANCE_STOP, MM_SOUND_ALL_STOP, MM_SOUND_INSTANCE_PLAYING, MM_SOUND_ALL_PLAYING,
        MM_SOUND_INSTANCE_PAUSE, MM_SOUND_ALL_PAUSE, MM_SOUND_INSTANCE_RESUME, MM_SOUND_ALL_RESUME
    };
    MMI32 first, second = 0, result = 0;
    int operation, available;
    if (!sound)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        return;
    }
    switch (opcode)
    {
    case kopPlaySoundGob: case kopPlaySoundThis:
        play_sound(vm, opcode, sound);
        return;
    case kopPlayMouseSoundGob: case kopPlayMouseSoundThis:
        play_mouse_sound(vm, opcode, sound);
        return;
    case kopGetMasterVolume:
        sound->control(vm->host, MM_SOUND_GET_VOLUME, 0, 0, &result);
        mm_script_vm_push(vm, result);
        return;
    case kopSetMasterVolume:
        operation = MM_SOUND_VOLUME;
        break;
    default:
        operation = operations[opcode - kopStopSound];
        break;
    }
    first = mm_script_vm_pop(vm);
    if (operation == MM_SOUND_ALL_STOP || operation == MM_SOUND_ALL_PAUSE ||
        operation == MM_SOUND_ALL_RESUME || operation == MM_SOUND_ALL_PLAYING)
        second = mm_script_vm_pop(vm);
    if (vm->error)
        return;
    available = sound->control(vm->host, operation, first, second, &result);
    /* Retail's playing queries consume the operands without pushing when
     * there is no mixer. The master volume query above always pushes. */
    if (available && (operation == MM_SOUND_INSTANCE_PLAYING || operation == MM_SOUND_ALL_PLAYING))
        mm_script_vm_push(vm, result != 0);
}

typedef union KidspaceText
{
    unsigned char narrow[MM_KIDSPACE_TEXT_LIMIT + 2];
    unsigned short wide[MM_KIDSPACE_TEXT_LIMIT + 2];
} KidspaceText;

static int message_number(void *context, unsigned control, MMStringFormatArgument *argument)
{
    const MMU32 **next = context;
    (void)control;
    argument->number = *(*next)++;
    return 1;
}

static const char message_prefix[] = "Script Message ('%f', 0x%x, %d): ";

static void format_message(const MMStringValue *out, const char *pattern, const MMU32 *words)
{
    unsigned short wide[sizeof(message_prefix)];
    MMStringFormatArguments arguments;
    MMI32 count = (MMI32)strlen(pattern), i;
    const void *native = pattern;
    if (out->unit == 2)
    {
        for (i = 0; i < count; ++i)
            wide[i] = (unsigned char)pattern[i];
        native = wide;
    }
    arguments.context = &words;
    arguments.next = message_number;
    arguments.bad_format = NULL;
    mm_string_format(out, native, count, &arguments);
}

static void show_message(MMScriptVM *vm, unsigned opcode, const MMKidspaceAppServices *app)
{
    const MMResourceObject *binding = mm_script_binding(vm->script);
    const int strings = opcode == kopAlertStr || opcode == kopPrintStr || opcode == kopPrintStrStat;
    int destination = MM_KIDSPACE_PRINT;
    MMI32 count = mm_script_vm_pop(vm), id;
    MMU32 words[3], length;
    const void *text;
    KidspaceText message, item;
    unsigned unit = vm->strings ? vm->strings->unit_bytes : mm_script_unit_bytes(vm->script);
    MMStringValue output, number;

    vm->kidspace->forget_current(vm->host);
    if (opcode == kopPrintStat || opcode == kopPrintStrStat)
    {
        /* In builds without chunk statistics only the count is consumed. */
        if (!app->statistics_enabled)
            return;
        destination = MM_KIDSPACE_STATISTICS;
    }
    else if (opcode == kopAlert || opcode == kopAlertStr)
        destination = MM_KIDSPACE_ALERT;
    if (strings && !vm->strings)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
    if (vm->error || (count > 0 && !mm_script_vm_words(vm, count)))
        return;

    words[0] = binding->type;
    words[1] = binding->number;
    words[2] = vm->pc;
    output.buffer = &message;
    output.unit = unit;
    output.context = NULL;
    output.check_copy = NULL;
    number = output;
    number.buffer = &item;
    format_message(&output, message_prefix, words);
    while (count > 0)
    {
        id = mm_script_vm_pop(vm);
        if (strings)
            mm_script_strings_get(vm->strings, id, &text, &length);
        else
        {
            words[0] = words[1] = (MMU32)id;
            format_message(&number, "%d (0x%x) ", words);
            length = unit == 1 ? item.narrow[0] : item.wide[0];
            text = (const unsigned char *)&item + unit;
        }
        mm_string_value_append(&output, text, (MMI32)length);
        --count;
    }
    length = unit == 1 ? message.narrow[0] : message.wide[0];
    app->message(vm->host, destination, (const unsigned char *)&message + unit, length);
}

static void edit_control(MMScriptVM *vm, unsigned opcode, const MMKidspaceAppServices *app)
{
    MMI32 id = mm_script_vm_pop(vm);
    MMI32 string_id = mm_script_vm_pop(vm);
    void *object = find(vm, id);
    KidspaceText output;
    const void *text;
    MMU32 count;

    if (!object || !app->is_edit(vm->host, object))
        notice(vm, MM_KIDSPACE_MISSING_EDIT, opcode, id, 0);
    else if (!vm->strings)
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
    else if (opcode == kopGetEdit)
    {
        count = app->read_edit(vm->host, object, &output, MM_KIDSPACE_TEXT_LIMIT);
        mm_script_strings_put(vm->strings, string_id, &output, count);
    }
    else
    {
        mm_script_strings_get(vm->strings, string_id, &text, &count);
        app->write_edit(vm->host, object, text, count);
    }
}

static void use_palette(MMScriptVM *vm, unsigned opcode, const MMKidspaceAppServices *app)
{
    MMScriptCollection *table = NULL;
    MMKidspacePalette palette;
    MMI32 kind = 0, parameter = 0;
    MMU32 ticks = 0, color = 0, number, duration;
    if (opcode == kopTransition)
    {
        kind = mm_script_vm_pop(vm);
        parameter = mm_script_vm_pop(vm);
        ticks = (MMU32)mm_script_vm_pop(vm);
        color = (MMU32)mm_script_vm_pop(vm);
    }
    number = (MMU32)mm_script_vm_pop(vm);
    if (vm->error)
        return;
    if (opcode == kopTransition)
        vm->kidspace->forget_current(vm->host);
    if (number != (MMU32)0xffffffffUL)
        table = app->load_palette(vm->host, number);
    if (table)
    {
        palette.rows = mm_script_color_rows(table, &palette.count);
        mm_script_collection_priority(table, MM_RESOURCE_TOSS_PRIORITY);
    }
    if (opcode == kopTransition)
    {
        /* Unsigned scaling without a 64-bit type. Keep the low word when
         * the quotient exceeds the 32-bit millisecond range. */
        duration = ticks / MM_CLOCK_TICKS_PER_SECOND * MM_CLOCK_MILLISECONDS_PER_SECOND +
                   ticks % MM_CLOCK_TICKS_PER_SECOND * MM_CLOCK_MILLISECONDS_PER_SECOND / MM_CLOCK_TICKS_PER_SECOND;
        app->transition(vm->host, kind, parameter, duration, color, table ? &palette : NULL);
    }
    else if (table)
        app->set_palette(vm->host, &palette);
    mm_script_collection_release(table);
}

static void app_command(MMScriptVM *vm, unsigned opcode)
{
    const MMKidspaceScriptServices *host = vm->kidspace;
    const MMKidspaceAppServices *app = host->app;
    MMI32 first, second = 0, result = 0;
    const void *text;
    MMU32 count;
    if (!app)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        return;
    }
    switch (opcode)
    {
    case kopAlert: case kopPrint: case kopPrintStat: case kopAlertStr: case kopPrintStr: case kopPrintStrStat:
        show_message(vm, opcode, app);
        return;
    case kopSetColorTable: case kopTransition:
        use_palette(vm, opcode, app);
        return;
    case kopGetEdit: case kopSetEdit:
        edit_control(vm, opcode, app);
        return;
    case kopGetModifierState:
        mm_script_vm_push(vm, mm_signed_word(app->modifiers(vm->host)));
        return;
    case kopStartLongOp:
        app->begin_work(vm->host);
        return;
    }

    first = mm_script_vm_pop(vm);
    if (opcode == kopChangeModifierState || opcode == kopSetProp || opcode == kopModalHelp)
        second = mm_script_vm_pop(vm);
    if (vm->error)
        return;
    switch (opcode)
    {
    case kopChangeModifierState:
        app->change_modifiers(vm->host, (MMU32)first, (MMU32)second);
        break;
    case kopGetProp:
        host->forget_current(vm->host);
        if (!app->get_property(vm->host, first, &result))
        {
            notice(vm, MM_KIDSPACE_GET_PROPERTY_FAILED, opcode, first, 0);
            result = 0;
        }
        mm_script_vm_push(vm, result);
        break;
    case kopSetProp:
        host->forget_current(vm->host);
        if (!app->set_property(vm->host, first, second))
            notice(vm, MM_KIDSPACE_SET_PROPERTY_FAILED, opcode, first, (MMU32)second);
        break;
    case kopLaunch:
        host->forget_current(vm->host);
        if (!vm->strings)
            mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPERAND);
        else if (!mm_script_strings_get(vm->strings, first, &text, &count))
            notice(vm, MM_KIDSPACE_LAUNCH_STRING_MISSING, opcode, first, 0);
        else
            result = app->launch(vm->host, text, count) != 0;
        mm_script_vm_push(vm, result);
        break;
    case kopEndLongOp:
        app->end_work(vm->host, first != 0);
        break;
    case kopModalHelp:
        host->forget_current(vm->host);
        if (app->modal_help(vm->host, (MMU32)first, &result))
            second = result;
        mm_script_vm_push(vm, second);
        break;
    case kopFlushUserEvents:
        app->flush_events(vm->host, (MMU32)first);
        break;
    }
}

static void object_command(MMScriptVM *vm, unsigned opcode, int explicit_id)
{
    if (!vm->kidspace->objects)
    {
        mm_script_vm_fail(vm, MM_SCRIPT_ERROR_OPCODE);
        return;
    }
    /* Each explicit-target opcode in this group immediately precedes its
     * current-object counterpart in the bytecode format. */
    opcode += explicit_id;
    switch (opcode)
    {
    case kopCreateChildThis: case kopCreateHelpThis: case kopDestroyThis: case kopDestroyChildrenThis:
        object_lifetime(vm, opcode, explicit_id);
        break;
    case kopStateThis: case kopZThis: case kopPlayingThis: case kopCurFrameThis: case kopCountFramesThis:
        object_query(vm, opcode, explicit_id);
        break;
    default:
        object_control(vm, opcode, explicit_id);
        break;
    }
}

int mm_kidspace_script_execute(MMScriptVM *vm, unsigned opcode)
{
    switch (opcode)
    {
    case kopCreateChildGob: case kopCreateHelpGob: case kopDestroyGob: case kopDestroyChildrenGob:
    case kopStateGob: case kopZGob: case kopPlayingGob: case kopCurFrameGob: case kopCountFramesGob:
    case kopChangeStateGob: case kopAnimateGob: case kopSetPictureGob: case kopSetRepGob: case kopSetZGob:
    case kopPlayGob: case kopStopGob: case kopGotoFrameGob: case kopFilterCmdsGob: case kopSetNoSlipGob:
    case kopSetToolTipSourceGob: case kopStreamGob:
        object_command(vm, opcode, 1);
        break;
    case kopCreateChildThis: case kopCreateHelpThis: case kopDestroyThis: case kopDestroyChildrenThis:
    case kopStateThis: case kopZThis: case kopPlayingThis: case kopCurFrameThis: case kopCountFramesThis:
    case kopChangeStateThis: case kopAnimateThis: case kopSetPictureThis: case kopSetRepThis: case kopSetZThis:
    case kopPlayThis: case kopStopThis: case kopGotoFrameThis: case kopFilterCmdsThis: case kopSetNoSlipThis:
    case kopSetToolTipSourceThis: case kopStreamThis:
        object_command(vm, opcode, 0);
        break;
    case kopResizeGob: case kopResizeThis:
        change_rect(vm, opcode, RESIZE, opcode == kopResizeGob);
        break;
    case kopMoveRelGob: case kopMoveRelThis:
        change_rect(vm, opcode, MOVE_RELATIVE, opcode == kopMoveRelGob);
        break;
    case kopMoveAbsGob: case kopMoveAbsThis:
        change_rect(vm, opcode, MOVE_ABSOLUTE, opcode == kopMoveAbsGob);
        break;
    case kopXMouseGob: case kopXMouseThis:
        query_point(vm, opcode, MM_KIDSPACE_MOUSE, opcode == kopXMouseGob, 0);
        break;
    case kopYMouseGob: case kopYMouseThis:
        query_point(vm, opcode, MM_KIDSPACE_MOUSE, opcode == kopYMouseGob, 1);
        break;
    case kopXGob: case kopXThis:
        query_point(vm, opcode, MM_KIDSPACE_PARENT_REGISTRATION, opcode == kopXGob, 0);
        break;
    case kopYGob: case kopYThis:
        query_point(vm, opcode, MM_KIDSPACE_PARENT_REGISTRATION, opcode == kopYGob, 1);
        break;
    case kopWidthGob: case kopWidthThis:
        query_extent(vm, opcode, opcode == kopWidthGob, 0);
        break;
    case kopHeightGob: case kopHeightThis:
        query_extent(vm, opcode, opcode == kopHeightGob, 1);
        break;
    case kopGidThis:
        mm_script_vm_push(vm, vm->this_object);
        break;
    case kopGidParGob: case kopGidParThis:
        query_relative(vm, MM_KIDSPACE_PARENT, opcode == kopGidParGob);
        break;
    case kopGidNextSib:
        query_relative(vm, MM_KIDSPACE_NEXT_SIBLING, 1);
        break;
    case kopGidPrevSib:
        query_relative(vm, MM_KIDSPACE_PREVIOUS_SIBLING, 1);
        break;
    case kopGidChild:
        query_relative(vm, MM_KIDSPACE_FIRST_CHILD, 1);
        break;
    case kopFGobExists:
        mm_script_vm_push(vm, find(vm, mm_script_vm_pop(vm)) != NULL);
        break;
    case kopFIsDescendent:
        query_descendent(vm);
        break;
    case kopGidUnique:
        mm_script_vm_push(vm, vm->kidspace->unique_id(vm->host));
        break;
    case kopEnqueueCid:
        enqueue(vm);
        break;
    case kopRunScriptGob: case kopRunScriptThis: case kopRunScriptCnoGob: case kopRunScriptCnoThis:
        run_script(vm, opcode);
        break;
    case kopCell: case kopCellNoPause:
        play_cell(vm, opcode);
        break;
    case kopCreateClock: case kopDestroyClock: case kopStartClock: case kopStopClock: case kopTimeCur:
    case kopSetAlarm: case kopSetAlarmThis: case kopSetAlarmGob:
        clock_command(vm, opcode);
        break;
    case kopPlaySoundThis: case kopPlaySoundGob: case kopPlayMouseSoundThis: case kopPlayMouseSoundGob:
    case kopStopSound: case kopStopSoundClass: case kopPlayingSound: case kopPlayingSoundClass:
    case kopPauseSound: case kopPauseSoundClass: case kopResumeSound: case kopResumeSoundClass:
    case kopSetMasterVolume: case kopGetMasterVolume:
        sound_command(vm, opcode);
        break;
    case kopAlert: case kopPrint: case kopPrintStat: case kopAlertStr: case kopPrintStr: case kopPrintStrStat:
    case kopSetColorTable: case kopTransition: case kopGetModifierState: case kopChangeModifierState:
    case kopGetEdit: case kopSetEdit: case kopGetProp: case kopSetProp: case kopLaunch:
    case kopStartLongOp: case kopEndLongOp: case kopModalHelp: case kopFlushUserEvents:
        app_command(vm, opcode);
        break;
    default:
        return 0;
    }
    return 1;
}
