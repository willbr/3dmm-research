/* Base token assignments converted from Kauai lex.h.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_LEX_TOKEN_H
#define MM_ENGINE_LEX_TOKEN_H

enum MMLexTokenKind
{
    ttNil,
    ttError, /* bad token */
    ttLong, /* numeric constant */
    ttName, /* identifier */
    ttString, /* string constant */

    ttAdd, /* + */
    ttSub, /* - */
    ttMul, /* * */
    ttDiv, /* / */
    ttMod, /* % */
    ttInc, /* ++ */
    ttDec, /* -- */
    ttBOr, /* | */
    ttBAnd, /* & */
    ttBXor, /* ^ */
    ttBNot, /* ~ */
    ttShr, /* >> */
    ttShl, /* << */
    ttLOr, /* || */
    ttLAnd, /* && */
    ttLXor, /* ^^ */
    ttEq, /* == */
    ttNe, /* != */
    ttGt, /* > */
    ttGe, /* >= */
    ttLt, /* < */
    ttLe, /* <= */
    ttLNot, /* ! */
    ttAssign, /* = */
    ttAAdd, /* += */
    ttASub, /* -= */
    ttAMul, /* *= */
    ttADiv, /* /= */
    ttAMod, /* %= */
    ttABOr, /* |= */
    ttABAnd, /* &= */
    ttABXor, /* ^= */
    ttAShr, /* >>= */
    ttAShl, /* <<= */
    ttArrow, /* -> */
    ttDot, /* . */
    ttQuery, /* ? */
    ttColon, /* : */
    ttComma, /* , */
    ttSemi, /* ; */
    ttOpenRef, /* [ */
    ttCloseRef, /* ] */
    ttOpenParen, /* ( */
    ttCloseParen, /* ) */
    ttOpenBrace, /* { */
    ttCloseBrace, /* } */
    ttPound, /* # */
    ttDollar, /* $ */
    ttAt, /* @ */
    ttAccent, /* ` */
    ttBackSlash, /* backslash character (\) */
    ttScope, /* :: */

    ttLimBase
};

#endif
