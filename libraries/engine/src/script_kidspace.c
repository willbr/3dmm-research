/* Kidspace script operations converted from Kauai screxeg.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/script_kidspace.h"
#include "mm/script_opcodes.h"
#include "mm/script_vm.h"
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
        vm->kidspace->cell(vm->host, object, child, x, y, delay);
    else
        notice(vm, MM_KIDSPACE_MISSING_OBJECT, opcode, vm->this_object, 0);
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
    object = id ? find(vm, id) : NULL;
    if (object && !sound->is_sound_object(vm->host, object))
        object = NULL;
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
    void *object = find(vm, id);
    MMI32 result = 0;
    if (object && sound->is_sound_object(vm->host, object))
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

int mm_kidspace_script_execute(MMScriptVM *vm, unsigned opcode)
{
    switch (opcode)
    {
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
    default:
        return 0;
    }
    return 1;
}
