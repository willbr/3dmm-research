/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Main include file for frame files.

***************************************************************************/
#ifndef FRAME_H
#define FRAME_H

#include "frameres.h" //frame resource id's
#include "util.h"
#include "keys.h"

class GraphicsPort;  // graphics port
class GraphicsEnvironment;  // graphics environment
class CommandHandler;  // command handler
class GraphicsObject;  // graphic object
class MenuBar;  // menu bar
class DocumentBase; // base document
class DocumentMDIWindow;  // document mdi window
class DocumentMainWindow;  // main document window
class DocumentScrollGraphicsObject;  // document scroll gob
class DocumentDisplayGraphicsObject;  // document display gob
class SoundManager; // sound manager

typedef class GraphicsPort *PGraphicsPort;
typedef class GraphicsEnvironment *PGraphicsEnvironment;
typedef class CommandHandler *PCommandHandler;
typedef class GraphicsObject *PGraphicsObject;
typedef class MenuBar *PMenuBar;
typedef class DocumentBase *PDocumentBase;
typedef class DocumentMDIWindow *PDocumentMDIWindow;
typedef class DocumentMainWindow *PDocumentMainWindow;
typedef class DocumentScrollGraphicsObject *PDocumentScrollGraphicsObject;
typedef class DocumentDisplayGraphicsObject *PDocumentDisplayGraphicsObject;
typedef class SoundManager *PSoundManager;

#include "region.h"
#include "pic.h"
#include "mbmp.h"
#include "gfx.h"
#include "cmd.h"
#include "cursor.h"
#include "appb.h"
#include "menu.h"
#include "gob.h"
#include "ctl.h"
#include "sndm.h"
#include "video.h"

// these are optional
#include "dlg.h"
#include "clip.h"
#include "docb.h"
#include "text.h"
#include "clok.h"
#include "textdoc.h"
#include "rtxt.h"
#include "chcm.h"
#include "spell.h"
#include "sndam.h"
#include "mididev.h"
#include "mididev2.h"

#endif //! FRAME_H
