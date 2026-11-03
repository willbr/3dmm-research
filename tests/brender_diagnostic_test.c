/* Core diagnostics: no GUI or application replacement symbols. */
#include "brender.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* These are the documented host diagnostic callbacks for the Kauai allocator. */
void WarnProc(char *file, long line, char *message)
{
    fprintf(stderr, "%s:%ld: %s\n", file, line, message);
}
int FAssertProc(char *file, long line, char *message, void *data, long length)
{
    WarnProc(file, line, message);
    exit(1);
    return 0;
}
static int warnings;
static void BR_CALLBACK Capture(char *message)
{
    ++warnings;
}
static br_diaghandler capture = {"test", Capture, Capture};

int main(int argc, char **argv)
{
    br_diaghandler *previous;
    BrBegin();
    if (argc > 1 && strcmp(argv[1], "fatal") == 0)
    {
        BrFailure("core fatal diagnostic");
        return 1;
    }
    previous = BrDiagHandlerSet(&capture);
    BrWarning("captured");
    if (warnings != 1 || BrDiagHandlerSet(previous) != &capture)
        return 2;
    BrWarning("core warning diagnostic");
    BrEnd();
    return 0;
}
