/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "diagnostics.h"
#include <cstdio>
namespace
{
thread_local bool assertionFailed = false;
thread_local char assertionMessage[256] = {};
} // namespace
namespace ThreeMM
{
namespace Internal
{
void ClearDiagnostic()
{
    assertionFailed = false;
    assertionMessage[0] = 0;
}
bool AssertionFailed()
{
    return assertionFailed;
}
void RecordAssertion(const char *message)
{
    if (!assertionFailed)
        std::snprintf(assertionMessage, sizeof(assertionMessage), "%s", message ? message : "Engine assertion");
    assertionFailed = true;
}
const char *DiagnosticMessage()
{
    return assertionMessage;
}
} // namespace Internal
} // namespace ThreeMM
