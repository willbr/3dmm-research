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
    default:
        return 0;
    }
    return 1;
}
