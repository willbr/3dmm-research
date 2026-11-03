/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_LAUNCH_CONTEXT_H
#define KAUAI_LAUNCH_CONTEXT_H
#include <cstdint>

namespace KauaiLaunch
{
#ifdef UNICODE
using NativeChar = std::uint16_t;
#else
using NativeChar = char;
#endif
// Original native command-line text, including the executable name and quotes.
// Always non-null; empty when an argument vector is active. No parsing or
// encoding conversion happens at this boundary.
const NativeChar *CommandLine();
// Copy before starting application readers/threads. A failed update leaves the
// previous value intact; a successful update invalidates previously read pointers.
// Null input is invalid; an empty string is a valid empty command line.
bool SetCommandLine(const NativeChar *text);
// Shell-separated native arguments, including argv[0]. Copies every string;
// quotes and spaces are literal and no synthetic raw command line is built.
// Zero arguments with a null array are valid. Negative counts or null elements
// fail without replacing the previous context. Same lifetime/thread rules as
// SetCommandLine. On POSIX, native arguments are byte strings.
bool SetArguments(int argc, const NativeChar *const *argv);
bool HasArguments();
int ArgumentCount();
const NativeChar *const *Arguments();
} // namespace KauaiLaunch
#endif
