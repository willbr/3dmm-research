/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Clipboard object declarations.

***************************************************************************/
#ifndef CLIP_H
#define CLIP_H
#include "mm/clipboard.h"

/***************************************************************************
    Clipboard object.
***************************************************************************/
typedef class ClipboardObject *PClipboardObject;
#define ClipboardObject_PAR BASE
#define kclsClipboardObject 'CLIP'
class ClipboardObject : public ClipboardObject_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    struct Host;
    MMClipboard _clipboard = {};

#ifdef KAUAI_SDL3
    HQ _hqExport;
    void *_pvExport;
#else
    HN _hnExport;
#endif
  public:
    ClipboardObject(void);

    bool FDocIsClip(PDocumentBase pdocb);
    void Show(void);

    void Set(PDocumentBase pdocb = pvNil, bool fExport = fTrue);
    bool FGetFormat(long cls, PDocumentBase *pdocb = pvNil);

    bool FInitExport(void);
    void *PvExport(long cb, long clfm);
    void EndExport(void);

    void Import(void);
};

extern PClipboardObject vpclip;

const long clfmNil = 0;
// Legacy format identifiers; SDL maps them to MIME types without an SDK.
// REVIEW shonk: Mac unicode
const long kclfmUniText = MacWin('WTXT', 13);
const long kclfmSbText = MacWin('TEXT', 1);

#ifdef UNICODE
const long kclfmText = kclfmUniText;
#else  //! UNICODE
const long kclfmText = kclfmSbText;
#endif //! UNICODE

#endif //! CLIP_H
