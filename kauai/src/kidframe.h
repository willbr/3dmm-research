/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Standard KidFrame header file.

***************************************************************************/
#ifndef KIDFRAME_H
#define KIDFRAME_H

#include "frame.h"
#include "kiddef.h"

// forward declarations
typedef class WorldOfKidspace *PWorldOfKidspace;
typedef class SCEG *PSCEG;
typedef class KidspaceGraphicObject *PKidspaceGraphicObject;
namespace Help {
   class HBAL;
   typedef class HBAL *PHBAL;
   struct HTOP;
   typedef struct HTOP *PHTOP;
}

#include "scrcomg.h"
#include "kidworld.h"
#include "screxeg.h"
#include "kidspace.h"
#include "kidhelp.h"

#endif //! KIDFRAME_H
