#ifndef MARCUS_PREVIEW_PROCESS_H
#define MARCUS_PREVIEW_PROCESS_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve the sibling helper on the owner thread. Release with SDL_free. */
char *marcus_preview_executable(void);
/* Start a background helper with null stdin and stderr. Capture stdout only
 * when requested. The argument array is borrowed through launch. */
SDL_Process *marcus_preview_process_start(const char *const *, int capture_output);
void marcus_preview_process_close(SDL_Process *);

#ifdef __cplusplus
}
#endif
#endif
