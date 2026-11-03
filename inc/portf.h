/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef STUDIO_PORTF_H
#define STUDIO_PORTF_H
#include "kauai_core.h"

/***************************************************************************

    Portfolio related includes.

    Primary Author: ******
    Review Status: Not yet reviewed

***************************************************************************/

// Top level portoflio routines.
bool FPortGetFniMovieOpen(Filename *pfni);
bool FPortDisplayWithIds(Filename *pfni, bool fOpen, LegacyLong lFilterLabel, LegacyLong lFilterExt, LegacyLong lTitle,
                         achar *lpstrDefExt, PString pstnDefFileName, Filename *pfniInitialDir, LegacyUlong grfPrevType,
                         ChunkNumber cnoWave);
bool FPortGetFniOpen(Filename *pfni, achar *lpstrFilter, achar *lpstrTitle, Filename *pfniInitialDir,
                     LegacyUlong grfPrevType, ChunkNumber cnoWave);
bool FPortGetFniSave(Filename *pfni, achar *lpstrFilter, achar *lpstrTitle, achar *lpstrDefExt, PString pstnDefFileName,
                     LegacyUlong grfPrevType, ChunkNumber cnoWave);

enum
{
    fpfNil = 0x0000,
    fpfPortPrevMovie = 0x0001,
    fpfPortPrevSound = 0x0002,
    fpfPortPrevTexture = 0x0004
};

#endif
