#ifndef MARCUS_STORYBOARD_DRAFT_H
#define MARCUS_STORYBOARD_DRAFT_H

#include "storyboard_edit_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusBoardSnapshot
{
    unsigned char *bytes;
    size_t size;
} MarcusBoardSnapshot;

/* Zero-initialize before first use. The draft owns its ink planes and snapshots. */
typedef struct MarcusBoardDraft
{
    unsigned char *planes[MARCUS_BOARD_ANGLE_LIMIT];
    int count;
    MarcusBoardSnapshot baseline, staged;
} MarcusBoardDraft;

void marcus_board_draft_dispose(MarcusBoardDraft *);
/* Replace the draft only after validating and copying all of the incoming data. */
int marcus_board_draft_load(MarcusBoardDraft *, const MarcusBoardWire *);
int marcus_board_draft_append(MarcusBoardDraft *);
unsigned char *marcus_board_draft_pixels(const MarcusBoardDraft *, int angle);
/* Views borrow the draft's paint or staged bytes. Never use a staged view after
 * staging again, accepting, or disposing the draft. */
int marcus_board_draft_view(const MarcusBoardDraft *, const MarcusBoardEditValues *, MarcusBoardWire *);
int marcus_board_draft_stage(MarcusBoardDraft *, const MarcusBoardEditValues *, int *changed);
int marcus_board_draft_staged(const MarcusBoardDraft *, MarcusBoardWire *);
void marcus_board_draft_accept(MarcusBoardDraft *);
void marcus_board_fallback_rgba(const unsigned char *pixels, const unsigned *resolved, size_t resolved_count,
                                const MarcusBoardEditValues *, unsigned char *rgba);

#ifdef __cplusplus
}
#endif
#endif
