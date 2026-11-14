/* Bounded CPU-only decode queue. SDL textures stay on the UI thread. */
#include "image_decode.h"
#include "image_view.h"
#include <string.h>

struct ReviewDecoder
{
    SDL_Thread *thread;
    SDL_Mutex *mutex;
    SDL_Condition *condition;
    int quit, busy, pending, ready;
    char folder[4096];
    ReviewDecodeResult request, result;
};
static const char *backend_names[4] = {"retail", "brender", "brender-compute", "sdl-gpu"};

void review_decode_free(ReviewDecodeResult *result)
{
    int i;
    for (i = 0; i < 4; ++i)
        SDL_DestroySurface(result->images[i]);
    memset(result, 0, sizeof(*result));
}
static SDL_Surface *read_png(const char *folder, int backend, int index)
{
    char path[8192];
    SDL_Surface *source, *rgba;
    SDL_snprintf(path, sizeof(path), "%s/%s/%06d.png", folder, backend_names[backend], index);
    source = SDL_LoadPNG(path);
    if (!source)
        return NULL;
    if (source->w != 544 || source->h != 306)
    {
        SDL_DestroySurface(source);
        SDL_SetError("Unexpected captured frame dimensions");
        return NULL;
    }
    rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(source);
    return rgba;
}
static void decode(const char *folder, ReviewDecodeResult *job)
{
    SDL_Surface *a, *b, *delta, *mask, *scaled;
    int y, i, count;
    a = job->images[0] = read_png(folder, 0, job->index);
    if (!a)
        goto failed;
    count = 1;
    if (job->wanted == 2)
    {
        b = job->images[1] = read_png(folder, job->backend + 1, job->index);
        delta = job->images[2] = SDL_CreateSurface(544, 306, SDL_PIXELFORMAT_RGBA32);
        mask = job->images[3] = SDL_CreateSurface(544, 306, SDL_PIXELFORMAT_RGBA32);
        if (!b || !delta || !mask)
            goto failed;
        for (y = 0; y < 306; ++y)
        {
            review_image_diff_row((unsigned char *)a->pixels + y * a->pitch,
                (unsigned char *)b->pixels + y * b->pitch, (unsigned char *)delta->pixels + y * delta->pitch, 544);
            memcpy((unsigned char *)mask->pixels + y * mask->pitch,
                   (unsigned char *)delta->pixels + y * delta->pitch, 544 * 4);
            review_image_highlight_row((unsigned char *)mask->pixels + y * mask->pitch, 544);
        }
        count = 4;
    }
    if (job->tier)
        for (i = 0; i < count; ++i)
        {
            scaled = SDL_ScaleSurface(job->images[i], (544 + (1 << job->tier) - 1) >> job->tier,
                     (306 + (1 << job->tier) - 1) >> job->tier, SDL_SCALEMODE_NEAREST);
            if (!scaled)
                goto failed;
            SDL_DestroySurface(job->images[i]);
            job->images[i] = scaled;
        }
    return;
failed:
    SDL_strlcpy(job->error, SDL_GetError(), sizeof(job->error));
}
static int SDLCALL decoder_thread(void *context)
{
    ReviewDecoder *decoder;
    ReviewDecodeResult job;
    char folder[4096];
    decoder = (ReviewDecoder *)context;
    for (;;)
    {
        SDL_LockMutex(decoder->mutex);
        while (!decoder->pending && !decoder->quit)
            SDL_WaitCondition(decoder->condition, decoder->mutex);
        if (decoder->quit)
        {
            SDL_UnlockMutex(decoder->mutex);
            return 0;
        }
        job = decoder->request;
        SDL_strlcpy(folder, decoder->folder, sizeof(folder));
        decoder->pending = 0;
        SDL_UnlockMutex(decoder->mutex);
        decode(folder, &job);
        SDL_LockMutex(decoder->mutex);
        decoder->result = job;
        decoder->ready = 1;
        SDL_UnlockMutex(decoder->mutex);
    }
}
ReviewDecoder *review_decoder_create(void)
{
    ReviewDecoder *decoder;
    decoder = (ReviewDecoder *)SDL_calloc(1, sizeof(*decoder));
    if (!decoder)
        return NULL;
    decoder->mutex = SDL_CreateMutex();
    decoder->condition = SDL_CreateCondition();
    if (decoder->mutex && decoder->condition)
        decoder->thread = SDL_CreateThread(decoder_thread, "comparison decode", decoder);
    if (!decoder->thread)
    {
        review_decoder_destroy(decoder);
        return NULL;
    }
    return decoder;
}
void review_decoder_destroy(ReviewDecoder *decoder)
{
    if (!decoder)
        return;
    if (decoder->thread)
    {
        SDL_LockMutex(decoder->mutex);
        decoder->quit = 1;
        SDL_SignalCondition(decoder->condition);
        SDL_UnlockMutex(decoder->mutex);
        SDL_WaitThread(decoder->thread, NULL);
    }
    review_decode_free(&decoder->result);
    SDL_DestroyCondition(decoder->condition);
    SDL_DestroyMutex(decoder->mutex);
    SDL_free(decoder);
}
int review_decoder_request(ReviewDecoder *decoder, const char *folder, int index, int backend,
                           int tier, int wanted, unsigned long token)
{
    int accepted;
    SDL_LockMutex(decoder->mutex);
    accepted = !decoder->busy;
    if (accepted)
    {
        SDL_strlcpy(decoder->folder, folder, sizeof(decoder->folder));
        memset(&decoder->request, 0, sizeof(decoder->request));
        decoder->request.index = index;
        decoder->request.backend = backend;
        decoder->request.tier = tier;
        decoder->request.wanted = wanted;
        decoder->request.token = token;
        decoder->busy = decoder->pending = 1;
        SDL_SignalCondition(decoder->condition);
    }
    SDL_UnlockMutex(decoder->mutex);
    return accepted;
}
int review_decoder_poll(ReviewDecoder *decoder, ReviewDecodeResult *result)
{
    int available;
    SDL_LockMutex(decoder->mutex);
    available = decoder->ready;
    if (available)
    {
        *result = decoder->result;
        memset(&decoder->result, 0, sizeof(decoder->result));
        decoder->ready = decoder->busy = 0;
    }
    SDL_UnlockMutex(decoder->mutex);
    return available;
}
