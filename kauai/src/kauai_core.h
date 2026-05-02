/* kauai_core.h -- umbrella header for the kauai-core (CLI runtime) library.
 *
 * Pulls in only the headers needed for chunky-file reading/writing,
 * file I/O, containers, byte-order helpers, and utilities. Does NOT
 * include frame.h or any UI-side headers (gob, gfx, dlg, kidspace,
 * appb, etc.), so consumers can compile and link without the WinMain
 * bootstrap and without dragging in Win32 windowing / multimedia libs
 * beyond what filewin/memwin already use.
 *
 * Use this from CLI tools (extract-bmdl, inspect-chunks, future
 * extract-tmpl) and headless test harnesses. For full kauai (with UI),
 * include frame.h instead.
 *
 * Implementation note: util.h is kauai's existing "kauai-core" umbrella
 * (it already pulls chunks/files/groups/etc. and handles the WIN/MAC
 * preamble around the platform-windows.h-equivalent include). Just
 * forward to it. util.h does pull in midi.h declarations, but no link
 * dependency: kauai-core does not include midi.cpp, so callers that
 * never reference midi symbols link cleanly.
 */
#ifndef KAUAI_CORE_H
#define KAUAI_CORE_H

#include "util.h"

#endif /* KAUAI_CORE_H */
