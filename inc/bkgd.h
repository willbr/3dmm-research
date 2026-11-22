/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    bkgd.h: Background class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    Native host services for C89 backgrounds.

***************************************************************************/
#ifndef BKGD_H
#define BKGD_H

#include <cstdint>

#include "background_data.h"
#include "background_renderer.h"

/****************************************
    Background Default Sound
****************************************/
// Authoring wire layout: 28 bytes with TAGOnFile. Runtime decoding uses
// mm_background_sound_read and never copies the stored pointer slot.
struct BackgroundDefaultSoundOnFile
{
    int16_t bo;
    int16_t osk;
    int32_t vlm;
    bool fLoop;
    TAGOnFile tagSnd;
};
static_assert(sizeof(BackgroundDefaultSoundOnFile) == 28, "BackgroundDefaultSoundOnFile wire format drift -- must stay 1995-compatible");
const ByteOrderMask kbomBds = 0x5f000000 | kbomTag >> 8;

namespace EngineBackground
{
inline const MMBackgroundOwnerHost &Host() { return mm_kauai_background_host()->owner; }
constexpr MMResourceBlockReader Read = mm_brender_background_read;
PBackground Fetch(PTAG);
PBackground Fetch(MMResourceCache *, ChunkTagOrType, ChunkNumber);
bool FCacheToHD(PTAG);
void GetName(PBackground, PString);
void TurnOnLights(PBackground, PWorld);
void TurnOffLights(PBackground);
bool FSetCamera(PBackground, PWorld, long camera);
void GetMouseMatrix(PBackground, BMAT34 *);
void GetActorPlacePoint(PBackground, BRS *, BRS *, BRS *);
void ReuseActorPlacePoint(PBackground);
bool FGetPalette(PBackground, PDynamicArray *, long *base);
void GetCameraPos(PBackground, BRS *, BRS *, BRS *);

inline MMBackgroundResource &Resource(PBackground background) { return *mm_background_owner_resource(background); }
inline MMBackgroundCameraState &Camera(PBackground background) { return *mm_background_owner_camera(background); }
inline bool FLeaveLitesOn(PBackground background) { return Resource(background).leave_on != 0; }
inline void SetFLeaveLitesOn(PBackground background, bool leaveOn) { Resource(background).leave_on = leaveOn; }
inline long Ccam(PBackground background) { return Camera(background).count; }
inline long Icam(PBackground background) { return Camera(background).current; }
inline BRA BraRotYCamera(PBackground background) { return Camera(background).yaw; }
inline void GetDefaultSound(PBackground background, PTAG tag, long *volume, bool *loop)
{
    const auto &sound = Resource(background).sound;
    TagFromCore(tag, sound.tag);
    *volume = sound.volume;
    *loop = sound.loop != 0;
}

#ifdef DEBUG
bool FWritePlaceFile(PBackground, BRS x, BRS y, BRS z);
void Validate(PBackground);
void Mark(PBackground);
#endif
}

#endif
