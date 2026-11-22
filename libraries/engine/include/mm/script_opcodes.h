/* Opcodes stored in legacy Kauai compiled scripts.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCRIPT_OPCODES_H
#define MM_ENGINE_SCRIPT_OPCODES_H
#ifdef __cplusplus
namespace ScriptCompiler {
#endif

/***************************************************************************
    Opcodes for scripts - these are the opcodes that can actually exist in
    script.  They don't necessarily map directly to the compiler's notion of
    an operator.
***************************************************************************/
/* If this enum changes, update the script versions in scrcom.h and scrcomg.h. */
enum MMScriptOpcode
{
    opNil,

    /* ops that act on variables come first */
    kopPushLocVar,
    kopPopLocVar,
    kopPushThisVar,
    kopPopThisVar,
    kopPushGlobalVar,
    kopPopGlobalVar,
    kopPushRemoteVar,
    kopPopRemoteVar,

    kopMinArray = 0x80,
    kopPushLocArray = kopMinArray,
    kopPopLocArray,
    kopPushThisArray,
    kopPopThisArray,
    kopPushGlobalArray,
    kopPopGlobalArray,
    kopPushRemoteArray,
    kopPopRemoteArray,
    kopLimArray,

    kopLimVarSccb = kopLimArray,

    /* ops that just act on the stack - no variables */
    /* for the comments below, a is the top value on the execution stack */
    /* b is the next to top value on the stack, and c is next on the stack */
    kopAdd = 0x100, /* addition: b + a */
    kopSub,         /* subtraction: b - a */
    kopMul,         /* multiplication: b * a */
    kopDiv,         /* integer division: b / a */
    kopMod,         /* mod: b % a */
    kopNeg,         /* unary negation: -a */
    kopInc,         /* unary increment: a + 1 */
    kopDec,         /* unary decrement: a - 1 */
    kopShr,         /* shift right: b >> a */
    kopShl,         /* shift left: b << a */
    kopBOr,         /* bitwise or: b | a */
    kopBAnd,        /* bitwise and: b & a */
    kopBXor,        /* bitwise exclusive or: b ^ a */
    kopBNot,        /* unary bitwise not: ~a */
    kopLXor,        /* logical exclusive or: (b != 0) != (a != 0) */
    kopLNot,        /* unary logical not: !a */
    kopEq,          /* equality: b == a */
    kopNe,          /* not equal: b != a */
    kopGt,          /* greater than: b > a */
    kopLt,          /* less than: b < a */
    kopGe,          /* greater or equal: b >= a */
    kopLe,          /* less or equal: b <= a */

    kopAbs,         /* unary absolute value: LwAbs(a) */
    kopRnd,         /* a random number between 0 and a - 1 (inclusive) */
    kopMulDiv,      /* a * b / c without loss of precision */
    kopDup,         /* duplicates the top value */
    kopPop,         /* discards the top value */
    kopSwap,        /* swaps the top two values */
    kopRot,         /* rotates b values by a (c is placed a slots deeper in the stack) */
    kopRev,         /* reverses the next a slots on the stack */
    kopDupList,     /* duplicates the next a slots on the stack */
    kopPopList,     /* pops the next a slots on the stack */
    kopRndList,     /* picks one of the next a stack values at random */
    kopSelect,      /* picks the b'th entry among the next a stack values */
    kopGoEq,        /* if (b == a) jumps to label c */
    kopGoNe,        /* if (b != a) jumps to label c */
    kopGoGt,        /* if (b > a) jumps to label c */
    kopGoLt,        /* if (b < a) jumps to label c */
    kopGoGe,        /* if (b >= a) jumps to label c */
    kopGoLe,        /* if (b <= a) jumps to label c */
    kopGoZ,         /* if (a == 0) jumps to label b */
    kopGoNz,        /* if (a != 0) jumps to label b */
    kopGo,          /* jumps to label a */
    kopExit,        /* terminates the script */
    kopReturn,      /* terminates the script with return value a */
    kopSetReturn,   /* sets the return value to a */
    kopShuffle,     /* shuffles the numbers 0,1,..,a-1 for calls to NextCard */
    kopShuffleList, /* shuffles the top a values for calls to NextCard */
    kopNextCard,    /* returns the next value from the shuffled values */
                    /* when all values have been used, the values are reshuffled */
    kopMatch,       /* a is a count of pairs, b is the key, c is the default value */
                    /* if b matches the first of any of the a pairs, the second */
                    /* value of the pair is pushed. if not, c is pushed. */
    kopPause,       /* pause the script (can be resumed later from C code) */
    kopCopyStr,     /* copy a string within the registry */
    kopMoveStr,     /* move a string within the registry */
    kopNukeStr,     /* delete a string from the registry */
    kopMergeStrs,   /* merge a string table into the registry */
    kopScaleTime,   /* scale the application clock */
    kopNumToStr,    /* convert a number to a decimal string */
    kopStrToNum,    /* convert a string to a number */
    kopConcatStrs,  /* concatenate two strings */
    kopLenStr,      /* return the number of characters in the string */
    kopCopySubStr,  /* copy a piece of the string */

    kopLimSccb
};

enum { MM_SCRIPT_LABEL_BYTE = 0xcc };

#ifdef __cplusplus
}
#endif
#endif
