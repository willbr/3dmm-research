/* Copyright (c) 3DMMForever contributors.
   Licensed under the MIT License. */

/***************************************************************************
    mcpserv.h: embedded MCP (Model Context Protocol) server for driving
    3dmovie.exe headlessly from Claude Code or any other MCP host.

    Active in DEBUG or opt-in STUDIO_TEST_TOOLS builds when the `--mcp-server` command-line
    switch is passed. The server runs JSON-RPC 2.0 over stdin/stdout. A worker
    thread reads requests off stdin and queues them; the main GUI thread drains
    the queue from `TopOfLoop` so all Kauai/SDL calls happen on the right
    thread.
***************************************************************************/
#pragma once

#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)

namespace mcp
{
// Returns true if `--mcp-server` was found in `pszCmdLine`. Cheap; safe to call
// before any kauai init. Framework console setup is independent of MCP.
bool FParseEnabledFromCommandLine(const char *pszCmdLine);

// Initialize the server: borrow inherited stdin/stdout, spawn the reader thread.
// Call once after the main window exists. No-op if mcp mode wasn't selected.
bool FInit(void);

// Drain queued requests, dispatch each to the appropriate tool handler on the
// current (main GUI) thread, write JSON responses back. Designed to be called
// from `ApplicationBase::TopOfLoop`. No-op if not in mcp mode.
void Drain(void);

// Stop and join the reader, including when the host keeps stdin open.
// Inherited stdio handles/descriptors remain owned by the process.
void Shutdown(void);

// True if mcp server mode is active for this process.
bool FActive(void);
} // namespace mcp

#endif // DEBUG || STUDIO_TEST_TOOLS
