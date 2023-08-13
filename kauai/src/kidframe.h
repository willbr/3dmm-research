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
namespace ScriptInterpreter {
   class GraphicsObjectInterpreter;
   typedef class GraphicsObjectInterpreter *PGraphicsObjectInterpreter;
}
namespace GraphicalObjectRepresentation {
   class KidspaceGraphicObject;
   typedef class KidspaceGraphicObject *PKidspaceGraphicObject;
   class GORP;
   typedef class GORP *PGORP;
}
namespace Help {
   class Balloon;
   typedef class Balloon *PBalloon;
   struct Topic;
   typedef struct Topic *PTopic;
}

using GraphicalObjectRepresentation::KidspaceGraphicObject;
using GraphicalObjectRepresentation::PKidspaceGraphicObject;
using GraphicalObjectRepresentation::GORP;
using GraphicalObjectRepresentation::PGORP;

#include "scrcomg.h"
#include "kidworld.h"
#include "screxeg.h"
#include "kidspace.h"
#include "kidhelp.h"

#endif //! KIDFRAME_H
