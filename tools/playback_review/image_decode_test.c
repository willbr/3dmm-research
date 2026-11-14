#include "image_decode.h"
#include <stdio.h>
#include <string.h>

static int wait_result(ReviewDecoder *decoder, ReviewDecodeResult *result)
{
    Uint64 deadline;
    deadline = SDL_GetTicks() + 5000;
    while (SDL_GetTicks() < deadline)
    {
        if (review_decoder_poll(decoder, result))
            return 1;
        SDL_Delay(1);
    }
    return 0;
}
int main(int argc, char **argv)
{
    SDL_Surface *image;
    ReviewDecoder *decoder;
    ReviewDecodeResult result;
    char path[8192];
    unsigned char *pixel;
    int code;
    if (argc != 2 || !SDL_Init(0) || !SDL_CreateDirectory(argv[1]))
        return 1;
    SDL_snprintf(path, sizeof(path), "%s/retail", argv[1]);
    if (!SDL_CreateDirectory(path))
        return 2;
    SDL_snprintf(path, sizeof(path), "%s/brender", argv[1]);
    if (!SDL_CreateDirectory(path))
        return 3;
    image = SDL_CreateSurface(544, 306, SDL_PIXELFORMAT_RGBA32);
    if (!image)
        return 4;
    SDL_FillSurfaceRect(image, NULL, SDL_MapSurfaceRGBA(image, 0, 0, 0, 255));
    SDL_snprintf(path, sizeof(path), "%s/retail/000000.png", argv[1]);
    if (!SDL_SavePNG(image, path))
        return 5;
    pixel = (unsigned char *)image->pixels + 4 * image->pitch + 3 * 4;
    pixel[0] = 255;
    SDL_snprintf(path, sizeof(path), "%s/brender/000000.png", argv[1]);
    if (!SDL_SavePNG(image, path))
        return 6;
    SDL_FillSurfaceRect(image, NULL, SDL_MapSurfaceRGBA(image, 0, 0, 0, 255));
    SDL_snprintf(path, sizeof(path), "%s/retail/000001.png", argv[1]);
    if (!SDL_SavePNG(image, path))
        return 16;
    SDL_FillSurfaceRect(image, NULL, SDL_MapSurfaceRGBA(image, 0, 255, 0, 255));
    SDL_snprintf(path, sizeof(path), "%s/brender/000001.png", argv[1]);
    if (!SDL_SavePNG(image, path))
        return 17;
    SDL_snprintf(path, sizeof(path), "%s/brender-asm", argv[1]);
    if (!SDL_CreateDirectory(path)) return 22;
    SDL_snprintf(path, sizeof(path), "%s/brender-asm/000000.png", argv[1]);
    if (!SDL_SavePNG(image, path)) return 23;
    SDL_DestroySurface(image);
    decoder = review_decoder_create();
    if (!decoder)
        return 7;
    code = 0;
    if (!review_decoder_request(decoder, argv[1], 0, 0, 0, 2, 17) ||
        review_decoder_request(decoder, argv[1], 0, 0, 0, 2, 18) || !wait_result(decoder, &result))
        code = 8;
    else
    {
        if (result.error[0] || result.token != 17 || !result.images[3])
            code = 9;
        else
        {
            pixel = (unsigned char *)result.images[2]->pixels + 4 * result.images[2]->pitch + 3 * 4;
            if (pixel[0] != 255 || pixel[1] || pixel[2] || pixel[3] != 255)
                code = 10;
            pixel = (unsigned char *)result.images[3]->pixels + 4 * result.images[3]->pitch + 3 * 4;
            if (pixel[0] != 255 || pixel[1] || pixel[2] != 255 || pixel[3] != 192 ||
                ((unsigned char *)result.images[3]->pixels)[3])
                code = 11;
        }
        review_decode_free(&result);
    }
    if (!code)
    {
        review_decoder_request(decoder, argv[1], 0, 0, 2, 1, 19);
        if (!wait_result(decoder, &result))
            code = 12;
        else
        {
            if (result.error[0] || !result.images[0] || result.images[0]->w != 136 ||
                result.images[0]->h != 77 || result.images[1])
                code = 13;
            review_decode_free(&result);
        }
    }
    if (!code)
    {
        review_decoder_request(decoder, argv[1], 2, 0, 0, 2, 20);
        if (!wait_result(decoder, &result))
            code = 14;
        else
        {
            if (!result.error[0])
                code = 15;
            review_decode_free(&result);
        }
    }
    if (!code)
    {
        review_decoder_request_atlas(decoder, argv[1], 0, 0, 2, 2, 2, 132, 21);
        if (!wait_result(decoder, &result))
            code = 18;
        else
        {
            if (result.error[0] || !result.images[2] || result.images[2]->w != 1088 || result.images[2]->h != 616)
                code = 19;
            else
            {
                pixel = (unsigned char *)result.images[2]->pixels + 136 * 4;
                if (pixel[0] || pixel[1] != 255 || pixel[2])
                    code = 20;
                pixel = (unsigned char *)result.images[3]->pixels + 136 * 4;
                if (pixel[0] != 255 || pixel[1] || pixel[2] != 255 || pixel[3] != 192)
                    code = 21;
            }
            review_decode_free(&result);
        }
    }
    if (!code)
    {
        review_decoder_request(decoder, argv[1], 0, 3, 0, 2, 22);
        if (!wait_result(decoder, &result)) code = 24;
        else
        {
            if (result.error[0] || !result.images[2] || result.backend != 3) code = 25;
            else
            {
                pixel = (unsigned char *)result.images[2]->pixels;
                if (pixel[0] || pixel[1] != 255 || pixel[2]) code = 26;
            }
            review_decode_free(&result);
        }
    }
    review_decoder_destroy(decoder);
    SDL_Quit();
    if (!code)
        puts("Background decode, diff, overlay, bounded queue and failure checks passed");
    return code;
}
