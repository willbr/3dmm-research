// Full movie runtime and document/view API.
#ifndef SOC_H
#define SOC_H
#include "frame.h"
#include "socutil.h"
#include "soc_core.h"
#include "bren.h"
#include "msnd.h"
#include "srec.h"
#include "actor.h"
#include "scene.h"
#include "movie.h"
#include "bkgd.h"
#include "tbox.h"

#define khidMscb khidLimKidFrame
#define khidMvieClock khidLimKidFrame + 1
#define khidRcd khidLimKidFrame + 2
#define khidMsq khidLimKidFrame + 3
#define khidMsqClock khidLimKidFrame + 4
// khidStudio 				khidLimKidFrame + 5  Defined in stdiodef.h

#endif
