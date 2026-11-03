/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Main include file for util files.

***************************************************************************/
#ifndef UTIL_H
#define UTIL_H
#include "legacy_types.h"
#include "core_platform.h"
#include "core_native.h"
#include "platform_files.h"
#include <stddef.h>
#ifdef DEBUG
#include <stdio.h>
#endif // DEBUG

// define the endian-ness
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LITTLE_ENDIAN
#elif defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM) || defined(_M_ARM64)
#define LITTLE_ENDIAN
#endif

#ifdef LITTLE_ENDIAN
#define BigLittle(a, b) b
#define Big(a)
#define Little(a) a
#else //! LITTLE_ENDIAN
#define BigLittle(a, b) a
#define Big(a) a
#define Little(a)
#endif //! LITTLE_ENDIAN

#ifdef MAC

#include "mac.h"

#endif

#define size(foo) ((LegacyLong)sizeof(foo))
#define offset(FOO, field) static_cast<LegacyLong>(offsetof(FOO, field))
#define CvFromRgv(rgv) (size(rgv) / size(rgv[0]))
#define BLOCK

#ifdef DEBUG
#define priv
#else //! DEBUG
#define priv static
#endif //! DEBUG

// standard scalar types
typedef unsigned char byte;
const byte kbMax = 0xFF;
const byte kbMin = 0;

typedef unsigned short ushort;
const short kswMax = (short)0x7FFF;
const short kswMin = -kswMax; // so -kswMin is positive
const ushort ksuMax = 0xFFFF;
const ushort ksuMin = 0;

typedef unsigned int uint;

typedef unsigned long ulong;
const LegacyLong klwMax = 0x7FFFFFFF;
const LegacyLong klwMin = -klwMax; // so -klwMin is positive
const LegacyUlong kluMax = 0xFFFFFFFF;
const LegacyUlong kluMin = 0;

// typedef int bool;

// standard character types:
// schar - short (skinny) character (1 byte)
// wchar - wide character (unicode)
// achar - application character
#ifdef MAC
typedef byte schar;
const schar kschMax = (schar)0xFF;
const schar kschMin = (schar)0;
#else  //! MAC
typedef char schar;
const schar kschMax = (schar)0x7F;
const schar kschMin = (schar)0x80;
#endif //! MAC
typedef schar *PSZS; // available before base/debug declarations, without SDK headers
typedef ushort wchar;
const wchar kwchMax = ksuMax;
const wchar kwchMin = ksuMin;
#ifdef UNICODE
typedef wchar achar;
typedef unsigned short uchar;
const achar kchMax = kwchMax;
const achar kchMin = kwchMin;
#define PszLit(sz) L##sz
#define ChLit(ch) L##ch
#else //! UNICODE
typedef schar achar;
typedef unsigned char uchar;
const achar kchMax = kschMax;
const achar kchMin = kschMin;
#define PszLit(sz) sz
#define ChLit(ch) ch
#endif //! UNICODE

namespace Group {
   class GroupBase;
   class VirtualArray;
   class DynamicArray;
   class AllocatedArray;
   typedef class GroupBase *PGroupBase;
   typedef class VirtualArray *PVirtualArray;
   typedef class DynamicArray *PDynamicArray;
   typedef class AllocatedArray *PAllocatedArray;

   class VirtualGroup;
   class GeneralGroup;
   class AllocatedGroup;
   typedef class VirtualGroup *PVirtualGroup;
   typedef class GeneralGroup *PGeneralGroup;
   typedef class AllocatedGroup *PAllocatedGroup;

   class VirtualStringTable;
   class StringTable_GST;
   class AllocatedStringTable;
   typedef class VirtualStringTable *PVirtualStringTable;
   typedef class StringTable_GST *PStringTable_GST;
   typedef class AllocatedStringTable *PAllocatedStringTable;
}

namespace ScriptInterpreter{
   class Script;
   typedef class Script *PScript;
}
typedef class DataBlock *PDataBlock;

#include "framedef.h"
#include "debug.h"
#include "base.h"
#include "utilint.h"
#include "utilcopy.h"
#include "utilstr.h"
#include "utilmem.h"
#include "fni.h"
#include "file.h"
#include "groups.h"
#include "utilerro.h"
#include "chunk.h"
#include "utilrnd.h"
#include "crf.h"
#include "codec.h"

// optional
#include "stream.h"
#include "lex.h"
#include "scrcom.h"
#include "screxe.h"
#include "chse.h"
#include "midi.h"

#include "utilglob.h"

#endif //! UTIL_H
