#ifndef MARCUS_ANIMATION_REQUEST_H
#define MARCUS_ANIMATION_REQUEST_H

#include "player.h"
#include "mm/actor_animation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apply a borrowed request. NULL reports a rejected request through the player. */
int marcus_edit_animation(MarcusPlayer *, int object, const MMActorAnimation *);

#ifdef __cplusplus
}
#endif
#endif
