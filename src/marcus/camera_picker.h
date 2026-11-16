#ifndef MARCUS_CAMERA_PICKER_H
#define MARCUS_CAMERA_PICKER_H
#include "ui.h"
#include "player.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusCameraPicker MarcusCameraPicker;
MarcusCameraPicker *camera_picker_create(void);
int camera_picker_visible(const MarcusCameraPicker *picker);
void camera_picker_destroy(MarcusCameraPicker *picker);
int camera_picker_show(MarcusCameraPicker *picker, MarcusPlayer *player);
int camera_picker_event(MarcusCameraPicker *picker, const SDL_Event *event, int width, int height);
void camera_picker_draw(MarcusCameraPicker *picker, MarcusUI *ui, int width, int height);
int camera_picker_take(MarcusCameraPicker *picker, unsigned long *camera);
/* 1 edits the selected storyboard angle, 2 adds a blank angle. */
int camera_picker_take_storyboard(MarcusCameraPicker *picker, int *angle);
#ifdef __cplusplus
}
#endif
#endif
