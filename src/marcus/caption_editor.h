#ifndef MARCUS_CAPTION_EDITOR_H
#define MARCUS_CAPTION_EDITOR_H
#include "player.h"
#include <SDL3/SDL.h>

typedef struct MarcusCaptionEditor MarcusCaptionEditor;
enum { CAPTION_REDRAW = 1, CAPTION_COMMITTED = 2 };
MarcusCaptionEditor *caption_editor_create(void);
void caption_editor_destroy(MarcusCaptionEditor *editor);
int caption_editor_show(MarcusCaptionEditor *editor, MarcusPlayer *player, const MarcusInfo *info, int add);
int caption_editor_visible(const MarcusCaptionEditor *editor);
void caption_editor_request_finish(MarcusCaptionEditor *editor, int apply);
int caption_editor_hit(const MarcusCaptionEditor *editor, const SDL_FRect *view, const MarcusImage *image,
                       float x, float y);
int caption_editor_draw(MarcusCaptionEditor *editor, MarcusPlayer *player, SDL_Renderer *renderer, const SDL_FRect *view,
                        const MarcusImage *image, const SDL_FRect *inspector);
#endif
