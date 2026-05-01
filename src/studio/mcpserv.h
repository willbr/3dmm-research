/* Copyright (c) 3DMMForever contributors.
   Licensed under the MIT License. */

/***************************************************************************
    mcpserv.h: embedded MCP (Model Context Protocol) server for driving
    3dmovie.exe headlessly from Claude Code or any other MCP host.

    Active only in DEBUG builds and only when the `--mcp-server` command-line
    switch is passed. The server runs JSON-RPC 2.0 over stdin/stdout. A worker
    thread reads requests off stdin and queues them; the main GUI thread drains
    the queue from `TopOfLoop` so all kauai/Win32 calls happen on the right
    thread.
***************************************************************************/
#pragma once

#ifdef DEBUG

namespace mcp
{
// Returns true if `--mcp-server` was found in `pszCmdLine`. Cheap; safe to call
// before any kauai init (used in WinMain to suppress AllocConsole).
bool FParseEnabledFromCommandLine(const char *pszCmdLine);

// Initialize the server: open stdin/stdout handles, spawn the reader thread.
// Call once after the main window exists. No-op if mcp mode wasn't selected.
bool FInit(void);

// Drain queued requests, dispatch each to the appropriate tool handler on the
// current (main GUI) thread, write JSON responses back. Designed to be called
// from `ApplicationBase::TopOfLoop`. No-op if not in mcp mode.
void Drain(void);

// Tear down the worker thread and stdio handles.
void Shutdown(void);

// True if mcp server mode is active for this process.
bool FActive(void);
} // namespace mcp

#endif // DEBUG
