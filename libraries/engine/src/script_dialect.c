/* Opcode descriptions converted from Kauai scrcom.cpp and scrcomg.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "script_compile_internal.h"
#include "mm/script_opcodes.h"
#include "mm/string_ops.h"

typedef struct OpcodeDescription
{
    const char *name, *infix_alias;
    MMScriptFunction function;
} OpcodeDescription;

#define OP(code, name) {name, NULL, {code, -1, 0, 0, 0}}
#define FN(code, name, fixed, group, minimum, returns_void) {name, NULL, {code, fixed, group, minimum, returns_void}}
#define ALIAS(code, name, alias, fixed, group, minimum, returns_void) \
    {name, alias, {code, fixed, group, minimum, returns_void}}

static const OpcodeDescription base[] = {
    OP(kopAdd, "Add"),
    OP(kopSub, "Sub"),
    OP(kopMul, "Mul"),
    OP(kopDiv, "Div"),
    OP(kopMod, "Mod"),
    FN(kopAbs, "Abs", 1, 0, 0, 0),
    OP(kopNeg, "Neg"),
    OP(kopInc, "Inc"),
    OP(kopDec, "Dec"),
    FN(kopRnd, "Rnd", 1, 0, 0, 0),
    FN(kopMulDiv, "MulDiv", 3, 0, 0, 0),
    OP(kopBAnd, "BAnd"),
    OP(kopBOr, "BOr"),
    OP(kopBXor, "BXor"),
    OP(kopBNot, "BNot"),
    OP(kopLXor, "LXor"),
    OP(kopLNot, "LNot"),
    OP(kopEq, "Eq"),
    OP(kopNe, "Ne"),
    OP(kopGt, "Gt"),
    OP(kopLt, "Lt"),
    OP(kopGe, "Ge"),
    OP(kopLe, "Le"),
    OP(kopDup, "Dup"),
    OP(kopPop, "Pop"),
    OP(kopSwap, "Swap"),
    OP(kopRot, "Rot"),
    OP(kopRev, "Rev"),
    OP(kopDupList, "DupList"),
    OP(kopPopList, "PopList"),
    FN(kopRndList, "RndList", 0, 1, 1, 0),
    FN(kopSelect, "Select", 1, 1, 1, 0),
    OP(kopGoEq, "GoEq"),
    OP(kopGoNe, "GoNe"),
    OP(kopGoGt, "GoGt"),
    OP(kopGoLt, "GoLt"),
    OP(kopGoGe, "GoGe"),
    OP(kopGoLe, "GoLe"),
    OP(kopGoZ, "GoZ"),
    ALIAS(kopGoNz, "GoNz", "IfGoto", 2, 0, 0, 1),
    ALIAS(kopGo, "Go", "Goto", 1, 0, 0, 1),
    FN(kopExit, "Exit", 0, 0, 0, 1),
    FN(kopReturn, "Return", 1, 0, 0, 1),
    FN(kopSetReturn, "SetReturn", 1, 0, 0, 1),
    FN(kopShuffle, "Shuffle", 1, 0, 0, 1),
    FN(kopShuffleList, "ShuffleList", 0, 1, 1, 1),
    FN(kopNextCard, "NextCard", 0, 0, 0, 0),
    FN(kopMatch, "Match", 2, 2, 1, 0),
    FN(kopPause, "Pause", 0, 0, 0, 1),
    FN(kopCopyStr, "CopyStr", 2, 0, 0, 0),
    FN(kopMoveStr, "MoveStr", 2, 0, 0, 0),
    FN(kopNukeStr, "NukeStr", 1, 0, 0, 1),
    FN(kopMergeStrs, "MergeStrs", 2, 0, 0, 1),
    FN(kopScaleTime, "ScaleTime", 1, 0, 0, 1),
    FN(kopNumToStr, "NumToStr", 2, 0, 0, 0),
    FN(kopStrToNum, "StrToNum", 3, 0, 0, 0),
    FN(kopConcatStrs, "ConcatStrs", 3, 0, 0, 0),
    FN(kopLenStr, "LenStr", 1, 0, 0, 0),
    FN(kopCopySubStr, "CopySubStr", 4, 0, 0, 0),
};

static const OpcodeDescription kidspace[] = {
    FN(kopCreateChildGob, "CreateChildGob", 3, 0, 0, 0),
    FN(kopCreateChildThis, "CreateChildThis", 2, 0, 0, 0),
    FN(kopDestroyGob, "DestroyGob", 1, 0, 0, 1),
    FN(kopDestroyThis, "DestroyThis", 0, 0, 0, 1),
    FN(kopResizeGob, "ResizeGob", 3, 0, 0, 1),
    FN(kopResizeThis, "ResizeThis", 2, 0, 0, 1),
    FN(kopMoveRelGob, "MoveRelGob", 3, 0, 0, 1),
    FN(kopMoveRelThis, "MoveRelThis", 2, 0, 0, 1),
    FN(kopMoveAbsGob, "MoveAbsGob", 3, 0, 0, 1),
    FN(kopMoveAbsThis, "MoveAbsThis", 2, 0, 0, 1),
    FN(kopGidThis, "GidThis", 0, 0, 0, 0),
    FN(kopGidParGob, "GidParGob", 1, 0, 0, 0),
    FN(kopGidParThis, "GidParThis", 0, 0, 0, 0),
    FN(kopGidNextSib, "GidNextSib", 1, 0, 0, 0),
    FN(kopGidPrevSib, "GidPrevSib", 1, 0, 0, 0),
    FN(kopGidChild, "GidChild", 1, 0, 0, 0),
    FN(kopFGobExists, "FGobExists", 1, 0, 0, 0),
    FN(kopCreateClock, "CreateClock", 1, 0, 0, 0),
    FN(kopDestroyClock, "DestroyClock", 1, 0, 0, 1),
    FN(kopStartClock, "StartClock", 2, 0, 0, 1),
    FN(kopStopClock, "StopClock", 1, 0, 0, 1),
    FN(kopTimeCur, "TimeCur", 1, 0, 0, 0),
    FN(kopSetAlarm, "SetAlarm", 2, 0, 0, 1),
    FN(kopEnqueueCid, "EnqueueCid", 6, 0, 0, 1),
    FN(kopAlert, "Alert", 0, 1, 1, 1),
    FN(kopRunScriptGob, "RunScriptGob", 2, 1, 0, 0),
    FN(kopRunScriptThis, "RunScriptThis", 1, 1, 0, 0),
    FN(kopStateGob, "StateGob", 1, 0, 0, 0),
    FN(kopStateThis, "StateThis", 0, 0, 0, 0),
    FN(kopChangeStateGob, "ChangeStateGob", 2, 0, 0, 1),
    FN(kopChangeStateThis, "ChangeStateThis", 1, 0, 0, 1),
    FN(kopAnimateGob, "AnimateGob", 2, 0, 0, 1),
    FN(kopAnimateThis, "AnimateThis", 1, 0, 0, 1),
    FN(kopSetPictureGob, "SetPictureGob", 2, 0, 0, 1),
    FN(kopSetPictureThis, "SetPictureThis", 1, 0, 0, 1),
    FN(kopSetRepGob, "SetRepGob", 2, 0, 0, 1),
    FN(kopSetRepThis, "SetRepThis", 1, 0, 0, 1),
    FN(kopXMouseGob, "XMouseGob", 1, 0, 0, 0),
    FN(kopXMouseThis, "XMouseThis", 0, 0, 0, 0),
    FN(kopYMouseGob, "YMouseGob", 1, 0, 0, 0),
    FN(kopYMouseThis, "YMouseThis", 0, 0, 0, 0),
    FN(kopGidUnique, "GidUnique", 0, 0, 0, 0),
    FN(kopXGob, "XGob", 1, 0, 0, 0),
    FN(kopXThis, "XThis", 0, 0, 0, 0),
    FN(kopYGob, "YGob", 1, 0, 0, 0),
    FN(kopYThis, "YThis", 0, 0, 0, 0),
    FN(kopZGob, "ZGob", 1, 0, 0, 0),
    FN(kopZThis, "ZThis", 0, 0, 0, 0),
    FN(kopSetZGob, "SetZGob", 2, 0, 0, 1),
    FN(kopSetZThis, "SetZThis", 1, 0, 0, 1),
    FN(kopSetColorTable, "SetColorTable", 1, 0, 0, 1),
    FN(kopCell, "Cell", 4, 0, 0, 1),
    FN(kopCellNoPause, "CellNoPause", 4, 0, 0, 1),
    FN(kopGetModifierState, "GetModifierState", 0, 0, 0, 0),
    FN(kopChangeModifierState, "ChangeModifierState", 2, 0, 0, 1),
    FN(kopCreateHelpGob, "CreateHelpGob", 2, 0, 0, 0),
    FN(kopCreateHelpThis, "CreateHelpThis", 1, 0, 0, 0),
    FN(kopTransition, "Transition", 5, 0, 0, 1),
    FN(kopGetEdit, "GetEdit", 2, 0, 0, 1),
    FN(kopSetEdit, "SetEdit", 2, 0, 0, 1),
    FN(kopAlertStr, "AlertStr", 0, 1, 1, 1),
    FN(kopGetProp, "GetProp", 1, 0, 0, 0),
    FN(kopSetProp, "SetProp", 2, 0, 0, 1),
    FN(kopLaunch, "Launch", 1, 0, 0, 0),
    FN(kopPlayGob, "PlayGob", 1, 0, 0, 1),
    FN(kopPlayThis, "PlayThis", 0, 0, 0, 1),
    FN(kopPlayingGob, "PlayingGob", 1, 0, 0, 0),
    FN(kopPlayingThis, "PlayingThis", 0, 0, 0, 0),
    FN(kopStopGob, "StopGob", 1, 0, 0, 1),
    FN(kopStopThis, "StopThis", 0, 0, 0, 1),
    FN(kopCurFrameGob, "CurFrameGob", 1, 0, 0, 0),
    FN(kopCurFrameThis, "CurFrameThis", 0, 0, 0, 0),
    FN(kopCountFramesGob, "CountFramesGob", 1, 0, 0, 0),
    FN(kopCountFramesThis, "CountFramesThis", 0, 0, 0, 0),
    FN(kopGotoFrameGob, "GotoFrameGob", 2, 0, 0, 1),
    FN(kopGotoFrameThis, "GotoFrameThis", 1, 0, 0, 1),
    FN(kopFilterCmdsGob, "FilterCmdsGob", 4, 0, 0, 1),
    FN(kopFilterCmdsThis, "FilterCmdsThis", 3, 0, 0, 1),
    FN(kopDestroyChildrenGob, "DestroyChildrenGob", 1, 0, 0, 1),
    FN(kopDestroyChildrenThis, "DestroyChildrenThis", 0, 0, 0, 1),
    FN(kopPlaySoundThis, "PlaySoundThis", 7, 0, 0, 0),
    FN(kopPlaySoundGob, "PlaySoundGob", 8, 0, 0, 0),
    FN(kopStopSound, "StopSound", 1, 0, 0, 1),
    FN(kopStopSoundClass, "StopSoundClass", 2, 0, 0, 1),
    FN(kopPlayingSound, "PlayingSound", 1, 0, 0, 0),
    FN(kopPlayingSoundClass, "PlayingSoundClass", 2, 0, 0, 0),
    FN(kopPauseSound, "PauseSound", 1, 0, 0, 1),
    FN(kopPauseSoundClass, "PauseSoundClass", 2, 0, 0, 1),
    FN(kopResumeSound, "ResumeSound", 1, 0, 0, 1),
    FN(kopResumeSoundClass, "ResumeSoundClass", 2, 0, 0, 1),
    FN(kopPlayMouseSoundGob, "PlayMouseSoundGob", 3, 0, 0, 1),
    FN(kopPlayMouseSoundThis, "PlayMouseSoundThis", 2, 0, 0, 1),
    FN(kopRunScriptCnoGob, "RunScriptCnoGob", 2, 1, 0, 0),
    FN(kopRunScriptCnoThis, "RunScriptCnoThis", 1, 1, 0, 0),
    FN(kopWidthGob, "WidthGob", 1, 0, 0, 0),
    FN(kopWidthThis, "WidthThis", 0, 0, 0, 0),
    FN(kopHeightGob, "HeightGob", 1, 0, 0, 0),
    FN(kopHeightThis, "HeightThis", 0, 0, 0, 0),
    FN(kopSetNoSlipGob, "SetNoSlipGob", 2, 0, 0, 1),
    FN(kopSetNoSlipThis, "SetNoSlipThis", 1, 0, 0, 1),
    FN(kopFIsDescendent, "FIsDescendent", 2, 0, 0, 0),
    FN(kopPrint, "Print", 0, 1, 1, 1),
    FN(kopPrintStr, "PrintStr", 0, 1, 1, 1),
    FN(kopSetMasterVolume, "SetMasterVolume", 1, 0, 0, 1),
    FN(kopGetMasterVolume, "GetMasterVolume", 0, 0, 0, 0),
    FN(kopStartLongOp, "StartLongOp", 0, 0, 0, 1),
    FN(kopEndLongOp, "EndLongOp", 1, 0, 0, 1),
    FN(kopSetToolTipSourceGob, "SetToolTipSourceGob", 2, 0, 0, 1),
    FN(kopSetToolTipSourceThis, "SetToolTipSourceThis", 1, 0, 0, 1),
    FN(kopSetAlarmGob, "SetAlarmGob", 4, 0, 0, 1),
    FN(kopSetAlarmThis, "SetAlarmThis", 3, 0, 0, 1),
    FN(kopModalHelp, "ModalHelp", 2, 0, 0, 0),
    FN(kopFlushUserEvents, "FlushUserEvents", 1, 0, 0, 1),
    FN(kopStreamGob, "StreamGob", 2, 0, 0, 1),
    FN(kopStreamThis, "StreamThis", 1, 0, 0, 1),
    FN(kopPrintStat, "PrintStat", 0, 1, 1, 1),
    FN(kopPrintStrStat, "PrintStrStat", 0, 1, 1, 1),
};

#undef ALIAS
#undef FN
#undef OP

static const OpcodeDescription *find_name(const OpcodeDescription *table, size_t count,
                                           const void *name, MMU32 length, unsigned unit, int infix)
{
    size_t i;
    const char *spelling;
    for (i = 0; i < count; ++i)
    {
        if (infix && table[i].function.fixed < 0)
            continue;
        spelling = infix && table[i].infix_alias ? table[i].infix_alias : table[i].name;
        if (mm_string_equals_ascii(spelling, name, length, unit))
            return table + i;
    }
    return NULL;
}

static const OpcodeDescription *lookup(int dialect, const void *name, MMU32 length, unsigned unit, int infix)
{
    const OpcodeDescription *entry = NULL;
    if (!name || !length || (unit != 1 && unit != 2))
        return NULL;
    if (dialect == MM_SCRIPT_KIDSPACE)
        entry = find_name(kidspace, sizeof(kidspace) / sizeof(*kidspace), name, length, unit, infix);
    return entry ? entry : find_name(base, sizeof(base) / sizeof(*base), name, length, unit, infix);
}

MMU32 mm_script_find_opcode(int dialect, const void *name, MMU32 length, unsigned unit)
{
    const OpcodeDescription *entry = lookup(dialect, name, length, unit, 0);
    return entry ? entry->function.opcode : opNil;
}

const MMScriptFunction *mm_script_find_function(int dialect, const void *name, MMU32 length, unsigned unit)
{
    const OpcodeDescription *entry = lookup(dialect, name, length, unit, 1);
    return entry ? &entry->function : NULL;
}

static const char *find_opcode(const OpcodeDescription *table, size_t count, MMU32 opcode)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (table[i].function.opcode == opcode)
            return table[i].name;
    return NULL;
}

const char *mm_script_opcode_name(int dialect, MMU32 opcode)
{
    const char *name = NULL;
    if (dialect == MM_SCRIPT_KIDSPACE)
        name = find_opcode(kidspace, sizeof(kidspace) / sizeof(*kidspace), opcode);
    return name ? name : find_opcode(base, sizeof(base) / sizeof(*base), opcode);
}
