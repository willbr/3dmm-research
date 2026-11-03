/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef ARCHIVE_DIAGNOSTICS_H
#define ARCHIVE_DIAGNOSTICS_H
namespace ThreeMM
{
namespace Internal
{
// Private bridge for the legacy core's process-wide diagnostic symbols.
void ClearDiagnostic();
bool AssertionFailed();
void RecordAssertion(const char *message);
const char *DiagnosticMessage();
} // namespace Internal
} // namespace ThreeMM
#endif
