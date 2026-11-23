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
#include <cstring>

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

static_assert(int(tdtsLim) == int(MM_TEXT_SHAPE_COUNT) && int(tdaLim) == int(MM_TEXT_ACTION_COUNT) &&
              tdtsCircleZ == 6 && tdaWalk == 13 && tdaStretch == 15, "text enums");

// A 3D text descriptor keeps all borrowed tag fields, including the cache slot.
inline MMTag TextFontTag(const TAG &tag)
{
    MMTag value = {tag.sid, nullptr, tag.ctg, tag.cno};
    std::memcpy(&value.cache, &tag.cache, sizeof(value.cache));
    return value;
}

#endif
