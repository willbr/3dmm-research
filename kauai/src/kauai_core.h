/* kauai_core.h -- umbrella header for the kauai-core (CLI runtime) library.
 *
 * Pulls in only the headers needed for chunky-file reading/writing,
 * file I/O, containers, byte-order helpers, and utilities. Does NOT
 * include frame.h or any UI-side headers (gob, gfx, dlg, kidspace,
 * appb, etc.), so consumers can compile and link without the WinMain
 * bootstrap or Windows SDK headers. Files and text use explicit platform
 * interfaces; native GDI implementations remain Windows-specific.
 *
 * Use this from CLI tools (extract-bmdl, inspect-chunks, future
 * extract-tmpl) and headless test harnesses. For full kauai (with UI),
 * include frame.h instead.
 *
 * util.h supplies the foundational types, containers, chunks and files.
 * Native interop uses opaque CoreNative tokens; GUI/application headers
 * are excluded. Command dispatch and clocks have separate cmd.h/clok.h
 * headers, and sound device interfaces have sndm.h.
 */
#ifndef KAUAI_CORE_H
#define KAUAI_CORE_H

// GUI adapters explicitly include frame.h/win_headers.h for native APIs.
#include "util.h"

#endif /* KAUAI_CORE_H */
