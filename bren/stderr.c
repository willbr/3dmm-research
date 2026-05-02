/*
 * Copyright (c) 1993 Argonaut Software Ltd. All rights reserved.
 *
 * $Id: stderr.c 1.3 1994/11/07 01:39:18 sam Exp $
 * $Locker: sam $
 *
 * Default error handler that reports error through stderr
 */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "brender.h"

/* C-linkage shim for kauai's AppendCrashLog (C++-mangled in appb.h).
 * Implementation lives in kauai/src/appbwin.cpp. Mirrors the message into
 * %TEMP%\3dmmforever-crash.txt with a category-tagged header so the agent
 * can read it via the existing read_crash_log MCP tool. */
extern void AppendCrashLog_C(const char *pszCategory, const char *pszBody);

static void BR_CALLBACK BrStdioWarning(char *message)
{
    AppendCrashLog_C("BRENDER_WARN", message);
    MessageBox(0, message, "BRender Warning", MB_OK);
}

static void BR_CALLBACK BrStdioError(char *message)
{
    /* Mirror to crash log first so the agent can read it via the existing
     * read_crash_log MCP tool even if the modal MessageBox blocks the GUI
     * thread. The list_dialogs tool also catches this dialog (it runs on
     * the MCP worker thread). */
    AppendCrashLog_C("BRENDER_FATAL", message);
    MessageBox(0, message, "BRender Fatal Error", MB_OK);
    exit(10);
}

/*
 * ErrorHandler structure
 */
br_diaghandler BrStdioDiagHandler = {
    "Stdio DiagHandler",
    BrStdioWarning,
    BrStdioError,
};

/*
 * Override default
 */
br_diaghandler *_BrDefaultDiagHandler = &BrStdioDiagHandler;
