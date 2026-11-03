/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "diagnostics.h"
#include "legacy_types.h"
// Separate archive member: existing legacy hosts may supply their own symbols.
void WarnProc(char *, LegacyLong, char *)
{
}
bool FAssertProc(char *, LegacyLong, char *message, void *, LegacyLong)
{
    ThreeMM::Internal::RecordAssertion(message);
    return false;
}
