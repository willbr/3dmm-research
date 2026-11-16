#ifndef MARCUS_RIBBON_H
#define MARCUS_RIBBON_H
#include "ui.h"
#define MARCUS_RIBBON_HEIGHT 144
enum MarcusRibbonAction
{
    RIBBON_NONE, RIBBON_NEW_MOVIE, RIBBON_OPEN, RIBBON_RESOURCES, RIBBON_CONTENT, RIBBON_SETTINGS,
    RIBBON_NEW_SCENE, RIBBON_SCENES, RIBBON_FRAMES, RIBBON_PREVIOUS, RIBBON_NEXT,
    RIBBON_OBJECTS, RIBBON_ACTOR, RIBBON_PROP, RIBBON_BOTH, RIBBON_SOUNDS, RIBBON_MUTE,
    RIBBON_RESET_MIX, RIBBON_WORDS, RIBBON_CAMERAS, RIBBON_CAMERA_GUIDES, RIBBON_NEW_PROP,
    RIBBON_STORYBOARD, RIBBON_SAVE_COPY, RIBBON_NEW_ACTOR, RIBBON_NEW_TEXT
};
void ribbon_draw(MarcusUI *ui, int width, int tab, const char *name, int loaded, int modified, int muted,
                 unsigned long cameras, int show_cameras);
int ribbon_hit(float x, float y, int width, int *tab, int loaded, unsigned long cameras);
#endif
