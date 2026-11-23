#include "preview_process.h"

char *marcus_preview_executable(void)
{
    const char *base = SDL_GetBasePath();
    char *path = NULL;
    if (base)
        SDL_asprintf(&path, "%smarcus-preview"
#ifdef _WIN32
                     ".exe"
#endif
                     , base);
    return path;
}

SDL_Process *marcus_preview_process_start(const char *const *args, int capture_output)
{
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Process *process = NULL;
    if (props)
    {
        if (SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args) &&
            SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, 1) &&
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL) &&
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                  capture_output ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL) &&
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL))
            process = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
    }
    return process;
}

void marcus_preview_process_close(SDL_Process *process)
{
    int status;
    if (!process)
        return;
    if (!SDL_WaitProcess(process, 0, &status))
    {
        SDL_KillProcess(process, 1);
        SDL_WaitProcess(process, 1, &status);
    }
    SDL_DestroyProcess(process);
}
