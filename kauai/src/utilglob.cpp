/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Contains the declarations of all globals used by util. The order of the
    globals within this file is critical. C++ specifies that constructors
    get called in source order within a module. It leaves construction
    order unspecified between modules. The mutexes at the top of this
    file should be constructed before anything else in the app. One way
    to guarantee this is for all globals to be in a single module. The
    app's global .cpp file can include utilglob.cpp (before any of its
    global declarations).

    For the MSVC tools, constructors seem to be executed in link order.

***************************************************************************/
#include "util.h"
ASSERTNAME

RTCLASS(UniversalScalableApplicationClock)

// MSVC initializes these utility objects before application globals.
#ifdef _MSC_VER
#pragma init_seg(lib)
#endif

// Global font tables and other objects release tracked allocations from their
// destructors. Their order across translation units is unspecified, so the two
// allocator locks must remain usable until process exit, including after this
// translation unit's destructors. Keep these locks for the process lifetime.

#ifdef DEBUG
// protects our debug linked list object management
Mutex &vmutxBase = *new Mutex;
#endif // DEBUG

Mutex &vmutxMem = *new Mutex;

// Shuffler and random number generator for the script interpreter
Shuffler vsflUtil;
Random vrndUtil;

// Standard Kauai codec
KauaiCodec vkcdcUtil;

// Standard compression manager - gets initialized with the standard
// Kauai codec. Clients can add additional codecs or redirect vpcodmUtil
// to a different compression manager with their own codecs
CodecManager vcodmUtil(&vkcdcUtil, kcfmtKauai2);
PCodecManager vpcodmUtil = &vcodmUtil;

// Standard scalable application clok.
UniversalScalableApplicationClock _usac;
PUniversalScalableApplicationClock vpusac = &_usac;

#ifdef DEBUG

// Debug memory globals
DebugMemoryGlobals vdmglob;

#endif // DEBUG
