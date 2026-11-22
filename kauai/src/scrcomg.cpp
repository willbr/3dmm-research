/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script compiler dialect and versions for GraphicsObject based scripts.

***************************************************************************/
#include "kidframe.h"
ASSERTNAME

namespace ScriptCompiler {

RTCLASS(GraphicsObjectCompiler)

int GraphicsObjectCompiler::_Dialect(void)
{
    return MM_SCRIPT_KIDSPACE;
}

/***************************************************************************
    Return the current version number of the script compiler.
***************************************************************************/
short GraphicsObjectCompiler::_SwCur(void)
{
    return kswCurSccg;
}

/***************************************************************************
    Return the back version number of the script compiler.  Versions
    back to here can read this script.
***************************************************************************/
short GraphicsObjectCompiler::_SwBack(void)
{
    return kswBackSccg;
}

/***************************************************************************
    Return the min version number of the script compiler.  We can read
    scripts back to this version.
***************************************************************************/
short GraphicsObjectCompiler::_SwMin(void)
{
    return kswMinSccg;
}

} // end of namespace ScriptCompiler
