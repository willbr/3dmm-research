#include "storyboard_draft.h"
#include <stdlib.h>
#include <string.h>

static int snapshot(const MarcusBoardWire *view, MarcusBoardSnapshot *out)
{
    size_t size = marcus_board_wire_size(view);
    unsigned char *bytes;
    if (!size)
        return 0;
    bytes = (unsigned char *)malloc(size);
    if (!bytes)
        return 0;
    if (!marcus_board_encode(view, bytes, size))
    {
        free(bytes);
        return 0;
    }
    free(out->bytes);
    out->bytes = bytes;
    out->size = size;
    return 1;
}

void marcus_board_draft_dispose(MarcusBoardDraft *draft)
{
    int i;
    for (i = 0; i < draft->count; ++i)
        free(draft->planes[i]);
    free(draft->baseline.bytes);
    free(draft->staged.bytes);
    memset(draft, 0, sizeof(*draft));
}

int marcus_board_draft_load(MarcusBoardDraft *draft, const MarcusBoardWire *board)
{
    MarcusBoardDraft next = {0};
    int i;
    if (!snapshot(board, &next.baseline))
        return 0;
    for (i = 0; i < board->values.angle_count; ++i)
    {
        unsigned char *plane = (unsigned char *)malloc(MARCUS_BOARD_PIXELS);
        if (!plane)
        {
            marcus_board_draft_dispose(&next);
            return 0;
        }
        memcpy(plane, board->pixels[i], MARCUS_BOARD_PIXELS);
        next.planes[next.count++] = plane;
    }
    marcus_board_draft_dispose(draft);
    *draft = next;
    return 1;
}

int marcus_board_draft_append(MarcusBoardDraft *draft)
{
    unsigned char *plane;
    if (draft->count >= MARCUS_BOARD_ANGLE_LIMIT)
        return 0;
    plane = (unsigned char *)calloc(MARCUS_BOARD_PIXELS, 1);
    if (!plane)
        return 0;
    draft->planes[draft->count++] = plane;
    return 1;
}

unsigned char *marcus_board_draft_pixels(const MarcusBoardDraft *draft, int angle)
{
    return angle >= 0 && angle < draft->count ? draft->planes[angle] : NULL;
}

int marcus_board_draft_view(const MarcusBoardDraft *draft, const MarcusBoardEditValues *values, MarcusBoardWire *out)
{
    int i;
    if (values->angle_count < 1 || values->angle_count != draft->count ||
        values->light_count < 0 || values->light_count > MARCUS_BOARD_LIGHT_LIMIT ||
        values->colour_count < MARCUS_BOARD_SWATCH_COUNT || values->colour_count > MARCUS_BOARD_COLOUR_LIMIT)
        return 0;
    memset(out, 0, sizeof(*out));
    out->values = *values;
    for (i = 0; i < draft->count; ++i)
        out->pixels[i] = draft->planes[i];
    return 1;
}

int marcus_board_draft_stage(MarcusBoardDraft *draft, const MarcusBoardEditValues *values, int *changed)
{
    MarcusBoardWire view;
    if (!marcus_board_draft_view(draft, values, &view) || !snapshot(&view, &draft->staged))
        return 0;
    *changed = draft->staged.size != draft->baseline.size ||
               memcmp(draft->staged.bytes, draft->baseline.bytes, draft->staged.size) != 0;
    return 1;
}

int marcus_board_draft_staged(const MarcusBoardDraft *draft, MarcusBoardWire *out)
{
    return marcus_board_decode(draft->staged.bytes, draft->staged.size, out) == MARCUS_BOARD_WIRE_OK;
}

void marcus_board_draft_accept(MarcusBoardDraft *draft)
{
    if (draft->staged.bytes)
    {
        free(draft->baseline.bytes);
        draft->baseline = draft->staged;
        draft->staged.bytes = NULL;
        draft->staged.size = 0;
    }
}

void marcus_board_fallback_rgba(const unsigned char *pixels, const unsigned *resolved, size_t resolved_count,
                                const MarcusBoardEditValues *values, unsigned char *rgba)
{
    int i;
    for (i = 0; i < MARCUS_BOARD_PIXELS; ++i, rgba += 4)
    {
        unsigned index = pixels[i];
        unsigned rgb = index < resolved_count ? resolved[index] :
                       index < (unsigned)values->colour_count ? values->colours[index] : 0;
        rgba[0] = (unsigned char)(rgb >> 16);
        rgba[1] = (unsigned char)(rgb >> 8);
        rgba[2] = (unsigned char)rgb;
        rgba[3] = 255;
    }
}
