#ifndef REVIEW_IMAGE_DECODE_H
#define REVIEW_IMAGE_DECODE_H
#include <SDL3/SDL.h>
typedef struct ReviewDecoder ReviewDecoder;
typedef struct ReviewDecodeResult
{
    int index, backend, tier, wanted;
    int atlas, retail_count, ours_count, total, revision;
    unsigned long token;
    SDL_Surface *images[4];
    char error[512];
} ReviewDecodeResult;
ReviewDecoder *review_decoder_create(void);
void review_decoder_destroy(ReviewDecoder *decoder);
int review_decoder_request(ReviewDecoder *decoder, const char *folder, int index, int backend,
                           int tier, int wanted, unsigned long token);
int review_decoder_poll(ReviewDecoder *decoder, ReviewDecodeResult *result);
int review_decoder_request_atlas(ReviewDecoder *decoder, const char *folder, int page, int backend,
                                 int retail_count, int ours_count, int total, int revision, unsigned long token);
void review_decode_free(ReviewDecodeResult *result);
#endif
