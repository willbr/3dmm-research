/* Scene sound-packet host facade, extracted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef SCENE_SOUND_H
#define SCENE_SOUND_H
#include "mm/scene_sound.h"
class Movie;
MMSceneSoundPacketServices ScenePacketServices(Movie *movie = nullptr);
void ReleasePpsse(MMSceneSound **sound);
#endif
