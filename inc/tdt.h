/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tdt.h: Three-D Text class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    Native font and archive services for generated C89 templates.

***************************************************************************/
#ifndef ThreeDText_H
#define ThreeDText_H
#include "mm/text_resource.h"

// 3-D Text Shapes - the positions and orientations of the letters
enum
{
    tdtsNil = -1,
    tdtsNormal = 0,
    tdtsArchPositive,
    tdtsCircleY,
    tdtsLargeMiddle,
    tdtsArchNegative,
    tdtsArchZ,
    tdtsCircleZ,
    tdtsVertical,
    tdtsGrowRight,
    tdtsGrowLeft,
    tdtsLim
};

// 3-D Actions
enum
{
    tdaNil = -1,
    tdaRest = 0,
    tdaLetterRotX, // each letter rotates around its own X axis
    tdaLetterRotY,
    tdaLetterRotZ,
    tdaSwingX, // letters skew right, then left, then back to normal
    tdaSwingY,
    tdaSwingZ,
    tdaPulse,    // letters grow 10%, then shrink back to normal
    tdaWordRotX, // (rotate entire word around X axis)
    tdaWordRotY,
    tdaWordRotZ,
    tdaWave,    // a bump ripples through the letters
    tdaReveal,  // letters slowly grow from zero height
    tdaWalk,    // walk (word hops forward as if walking)
    tdaHop,     // letters hop up and down
    tdaStretch, // stretch in X
    tdaLim
};

using PThreeDText = MMTemplateOwner *;

namespace EngineText
{
MMTextGenerationServices GenerationServices();
bool Initialize(PThreeDText, MMResourceCache *, ChunkTagOrType, ChunkNumber, MMOwnedBlock *);
void SetFontResolver(PThreeDText, void *context, MMFontOwner *(*)(void *, const MMTag *));
MMActionResource *FetchAction(PThreeDText, LegacyLong);
PModel FetchModel(PThreeDText, ChildChunkID);
bool ActionName(PThreeDText, LegacyLong, PString);

PThreeDText PtdtNew(PString, LegacyLong shape, PTAG font);
PThreeDText PtdtNew(PString, LegacyLong shape, PTAG font, const MMTemplateFontProvider &);
PDynamicArray PgltagFetch(PChunkyFile, ChunkTagOrType, ChunkNumber, bool *error);
PThreeDText PtdtDup(PThreeDText);
void GetInfo(PThreeDText, PString, LegacyLong *shape, PTAG font);
bool FChange(PThreeDText, PString, LegacyLong shape = tdtsNil, PTAG font = pvNil);
bool FWrite(PThreeDText, PChunkyFile, ChunkTagOrType, ChunkNumber *);
bool FWrite(PThreeDText, const MMArchiveWriter &, ChunkTagOrType, ChunkNumber *);
bool FWriteDraft(PChunkyFile, PString, LegacyLong shape, const TAG &font, ChunkNumber *);
bool FAdjustBody(PThreeDText, PBody);
bool FSetDefaultCost(PThreeDText, PBody);
}

#endif
