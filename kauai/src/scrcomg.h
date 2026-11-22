/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Script compiler for the gob based scripts.

***************************************************************************/
#ifndef SCRCOMG_H
#define SCRCOMG_H
#include "mm/script_opcodes.h"

namespace ScriptCompiler {

const short kswCurSccg = 0x101D;  // this version
const short kswBackSccg = 0x101D; // we can be read back to this version
const short kswMinSccg = 0x1015;  // we can read back to this version

/****************************************
    Gob based script compiler
****************************************/
typedef class GraphicsObjectCompiler *PGraphicsObjectCompiler;
#define GraphicsObjectCompiler_PAR CompilerBase
#define kclsGraphicsObjectCompiler 'SCCG'
class GraphicsObjectCompiler : public GraphicsObjectCompiler_PAR
{
    RTCLASS_DEC

  protected:
    virtual short _SwCur(void);
    virtual short _SwBack(void);
    virtual short _SwMin(void);

    int _Dialect(void) override;
};

} // end of namespace ScriptCompiler

#endif //! SCRCOMG_H
