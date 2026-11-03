/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

// Native file dialogs belong to the GUI runtime, not the core file API.
// Keep the existing Filename methods so current GUI callers retain their API.
#include "win_headers.h"
#include "util.h"
#include <commdlg.h>
ASSERTNAME

typedef OPENFILENAME OFN;

/***************************************************************************
    Get an fni (for opening) from the user.
***************************************************************************/
bool Filename::FGetOpen(achar *prgchFilter, HWND hwndOwner)
{
    AssertThis(0);
    AssertNilOrVarMem(prgchFilter);

    OFN ofn;
    ZString sz;

    ClearPb(&ofn, size(OFN));
    SetNil();

    sz[0] = 0;
    ofn.lStructSize = size(OFN);
    ofn.hwndOwner = hwndOwner;
    ofn.hInstance = NULL;
    ofn.lpstrFilter = prgchFilter;
    ofn.nFilterIndex = 1L;
    ofn.lpstrFile = sz;
    ofn.nMaxFile = kcchMaxSz;
    ofn.lpstrFileTitle = NULL;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOTESTFILECREATE | OFN_READONLY;

    if (!GetOpenFileName(&ofn))
    {
        SetNil();
        return fFalse;
    }

    _stnFile = ofn.lpstrFile;
    _SetFtgFromName();
    AssertThis(ffniFile);
    return fTrue;
}

/***************************************************************************
    Get an fni (for saving) from the user.
***************************************************************************/
bool Filename::FGetSave(achar *prgchFilter, HWND hwndOwner)
{
    AssertThis(0);
    AssertNilOrVarMem(prgchFilter);

    OFN ofn;
    ZString sz;

    ClearPb(&ofn, size(OFN));
    SetNil();

    sz[0] = 0;
    ofn.lStructSize = size(OFN);
    ofn.hwndOwner = hwndOwner;
    ofn.hInstance = NULL;
    ofn.lpstrFilter = prgchFilter;
    ofn.nFilterIndex = 1L;
    ofn.lpstrFile = sz;
    ofn.nMaxFile = kcchMaxSz;
    ofn.lpstrFileTitle = NULL;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = PszLit("");

    if (!GetSaveFileName(&ofn))
    {
        SetNil();
        return fFalse;
    }

    _stnFile = sz;
    _SetFtgFromName();
    AssertThis(ffniFile);
    return fTrue;
}
