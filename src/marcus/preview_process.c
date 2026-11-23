#include "preview_process.h"
#include <stdlib.h>
#include <string.h>

enum { PREVIEW_REPLY_LIMIT = 5 * 1024 * 1024, FRAME_REPLY_LIMIT = 256 * 1024 };

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

SDL_Process *marcus_preview_process_start(const char *const *args, int send_input, int capture_output)
{
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Process *process = NULL;
    if (props)
    {
        if (SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args) &&
            SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, 1) &&
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                                  send_input ? SDL_PROCESS_STDIO_APP : SDL_PROCESS_STDIO_NULL) &&
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

void marcus_preview_reply_dispose(MarcusPreviewReply *reply)
{
    free(reply->bytes);
    memset(reply, 0, sizeof(*reply));
}

static int publish_reply(MarcusPreviewReply *reply, unsigned char *bytes, size_t size)
{
    MarcusPreviewPacket packet;
    const int status = marcus_preview_wire_read(bytes, size, &packet);
    if (status != MARCUS_PREVIEW_WIRE_OK)
    {
        free(bytes);
        return status == MARCUS_PREVIEW_WIRE_HEADER ? MARCUS_PREVIEW_TRANSFER_HEADER : MARCUS_PREVIEW_TRANSFER_PACKET;
    }
    marcus_preview_reply_dispose(reply);
    reply->bytes = bytes;
    reply->packet = packet;
    return MARCUS_PREVIEW_TRANSFER_OK;
}

typedef struct PreviewDeadline
{
    void *context;
    MarcusPreviewCancelled *cancelled;
    Uint64 end;
    Uint32 poll;
} PreviewDeadline;

static int expired(const PreviewDeadline *deadline)
{
    return (deadline->cancelled && deadline->cancelled(deadline->context)) || SDL_GetTicks() >= deadline->end;
}

static int capture_reply(SDL_Process *process, const PreviewDeadline *deadline, MarcusPreviewReply *reply)
{
    SDL_IOStream *stream = SDL_GetProcessOutput(process);
    unsigned char buffer[65536], *bytes = NULL;
    size_t size = 0, capacity = 0;
    int status = MARCUS_PREVIEW_TRANSFER_OK, exit_status = -1;
    if (!stream)
        return MARCUS_PREVIEW_TRANSFER_IO;
    for (;;)
    {
        size_t count;
        if (expired(deadline))
        {
            status = MARCUS_PREVIEW_TRANSFER_CANCELLED;
            break;
        }
        count = SDL_ReadIO(stream, buffer, sizeof(buffer));
        if (!count)
        {
            if (SDL_GetIOStatus(stream) != SDL_IO_STATUS_NOT_READY)
                break;
            SDL_Delay(deadline->poll);
            continue;
        }
        if (count > PREVIEW_REPLY_LIMIT - size)
        {
            status = MARCUS_PREVIEW_TRANSFER_LIMIT;
            break;
        }
        if (size + count > capacity)
        {
            size_t next = capacity ? 2 * capacity : sizeof(buffer);
            unsigned char *grown;
            if (next < size + count)
                next = size + count;
            if (next > PREVIEW_REPLY_LIMIT)
                next = PREVIEW_REPLY_LIMIT;
            grown = realloc(bytes, next);
            if (!grown)
            {
                status = MARCUS_PREVIEW_TRANSFER_MEMORY;
                break;
            }
            bytes = grown;
            capacity = next;
        }
        memcpy(bytes + size, buffer, count);
        size += count;
    }
    /* A child can close stdout while it is still running. */
    while (!status && !SDL_WaitProcess(process, 0, &exit_status))
    {
        if (expired(deadline))
            status = MARCUS_PREVIEW_TRANSFER_CANCELLED;
        else
            SDL_Delay(deadline->poll);
    }
    if (!status && !exit_status)
        return publish_reply(reply, bytes, size);
    free(bytes);
    return status ? status : MARCUS_PREVIEW_TRANSFER_EXIT;
}

int marcus_preview_process_capture(const char *const *args, int duration, void *p, MarcusPreviewCancelled *cancelled,
                                    MarcusPreviewReply *reply)
{
    SDL_Process *process = marcus_preview_process_start(args, 0, 1);
    PreviewDeadline deadline;
    int status;
    if (!process)
        return MARCUS_PREVIEW_TRANSFER_START;
    deadline.context = p;
    deadline.cancelled = cancelled;
    deadline.end = SDL_GetTicks() + (duration ? 300000UL : 30000UL);
    deadline.poll = 5;
    status = capture_reply(process, &deadline, reply);
    marcus_preview_process_close(process);
    return status;
}

static int transfer(SDL_IOStream *stream, const unsigned char *send, unsigned char *receive, size_t size,
                     const PreviewDeadline *deadline)
{
    size_t offset = 0;
    while (offset < size)
    {
        size_t count;
        if (expired(deadline))
            return MARCUS_PREVIEW_TRANSFER_CANCELLED;
        count = send ? SDL_WriteIO(stream, send + offset, size - offset)
                     : SDL_ReadIO(stream, receive + offset, size - offset);
        if (!count)
        {
            if (SDL_GetIOStatus(stream) != SDL_IO_STATUS_NOT_READY)
                return send ? MARCUS_PREVIEW_TRANSFER_WRITE : MARCUS_PREVIEW_TRANSFER_STOPPED;
            SDL_Delay(deadline->poll);
        }
        offset += count;
    }
    return MARCUS_PREVIEW_TRANSFER_OK;
}

int marcus_preview_process_request(SDL_Process *process, const char *request, size_t bytes, int text, void *p,
                                    MarcusPreviewCancelled *cancelled, MarcusPreviewReply *reply)
{
    SDL_IOStream *input = SDL_GetProcessInput(process), *stream = SDL_GetProcessOutput(process);
    unsigned char prefix[MARCUS_PREVIEW_PREFIX_BYTES], *data;
    PreviewDeadline deadline;
    size_t size;
    int status;
    if (!input || !stream)
        return MARCUS_PREVIEW_TRANSFER_IO;
    deadline.context = p;
    deadline.cancelled = cancelled;
    deadline.end = SDL_GetTicks() + (text ? 30000UL : 60000UL);
    deadline.poll = text ? 1 : 5;
    status = transfer(input, (const unsigned char *)request, NULL, bytes, &deadline);
    if (status)
        return status;
    /* Frame requests give each transfer its own deadline. Text shares one. */
    if (!text)
        deadline.end = SDL_GetTicks() + 60000UL;
    status = transfer(stream, NULL, prefix, sizeof(prefix), &deadline);
    if (status)
        return status;
    size = marcus_preview_wire_frame_size(prefix, text ? PREVIEW_REPLY_LIMIT : FRAME_REPLY_LIMIT);
    if (!size)
        return MARCUS_PREVIEW_TRANSFER_LIMIT;
    data = malloc(size);
    if (!data)
        return MARCUS_PREVIEW_TRANSFER_MEMORY;
    if (!text)
        deadline.end = SDL_GetTicks() + 60000UL;
    status = transfer(stream, NULL, data, size, &deadline);
    if (!status)
        return publish_reply(reply, data, size);
    free(data);
    return status;
}
