#ifndef MARCUS_PREVIEW_PROCESS_H
#define MARCUS_PREVIEW_PROCESS_H

#include "preview_wire.h"
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve the sibling helper on the owner thread. Release with SDL_free. */
char *marcus_preview_executable(void);
/* Start a background helper. A disabled pipe and stderr use null stdio.
 * The argument array is borrowed through launch. */
SDL_Process *marcus_preview_process_start(const char *const *, int send_input, int capture_output);
void marcus_preview_process_close(SDL_Process *);

typedef struct MarcusPreviewReply
{
    unsigned char *bytes;
    MarcusPreviewPacket packet;
} MarcusPreviewReply;

typedef int MarcusPreviewCancelled(void *);
enum
{
    MARCUS_PREVIEW_TRANSFER_OK, MARCUS_PREVIEW_TRANSFER_START, MARCUS_PREVIEW_TRANSFER_IO,
    MARCUS_PREVIEW_TRANSFER_CANCELLED, MARCUS_PREVIEW_TRANSFER_WRITE, MARCUS_PREVIEW_TRANSFER_STOPPED,
    MARCUS_PREVIEW_TRANSFER_LIMIT, MARCUS_PREVIEW_TRANSFER_EXIT, MARCUS_PREVIEW_TRANSFER_MEMORY,
    MARCUS_PREVIEW_TRANSFER_HEADER, MARCUS_PREVIEW_TRANSFER_PACKET
};

/* Supply an initially zeroed reply. A successful transfer replaces it and
 * publishes packet views into the owned bytes. Failure preserves the reply.
 * Cancellation callbacks run synchronously and must not unwind. */
int marcus_preview_process_capture(const char *const *, int duration, void *, MarcusPreviewCancelled *,
                                    MarcusPreviewReply *);
/* The caller retains the child. Close it on any failed framed transfer. */
int marcus_preview_process_request(SDL_Process *, const char *request, size_t bytes, int text, void *,
                                    MarcusPreviewCancelled *, MarcusPreviewReply *);
void marcus_preview_reply_dispose(MarcusPreviewReply *);

#ifdef __cplusplus
}
#endif
#endif
