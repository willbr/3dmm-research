/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef SCENE_DATA_H
#define SCENE_DATA_H
#include "kauai_core.h"
#include <cstdint>
#include "mm/scene_data.h"
#include "mm/scene_track.h"

enum TRANS
{
    transNil = -1,
    transCut,
    transFadeToBlack,
    transFadeToWhite,
    transDissolve,
    transBlack,
    transLim
};
enum WaitReason
{
    witNil,
    witUntilClick,
    witUntilSnd,
    witForTime,
    witLim
};
enum SceneEventType
{                   // StartEv	FrmEv	Param
    sevtAddActr = MM_SCENE_ADD_ACTOR,        // Start event: actor or child ID.
    sevtPlaySnd = MM_SCENE_PLAY_SOUND,       // Frame event: sound packet.
    sevtAddTbox = MM_SCENE_ADD_TEXT,         // Start event: text box or child ID.
    sevtChngCamera = MM_SCENE_CHANGE_CAMERA, // Frame event: camera index.
    sevtSetBkgd = MM_SCENE_SET_BACKGROUND,  // Start event: background tag.
    sevtPause = MM_SCENE_PAUSE              // Frame event: type and duration.
};

//
// Struct for saving event pause information
//
struct SceneEventPause
{
    WaitReason wit;
    int32_t dts;
};
static_assert(sizeof(SceneEventPause) == 8, "SceneEventPause on-disk layout drift");

struct SceneEvent
{
    int32_t nfrm;        // frame number of the event.
    SceneEventType sevt; // event type
};
static_assert(sizeof(SceneEvent) == MM_SCENE_EVENT_BYTES, "SceneEvent on-disk layout drift");

const auto kbomSev = 0xF0000000;
const auto kbomSceneLong = 0xC0000000;

//
// Header for the scene chunk when on file
//
struct SceneOnFile
{
    int16_t bo;
    int16_t osk;
    int32_t nfrmLast;
    int32_t nfrmFirst;
    TRANS trans;
};
static_assert(sizeof(SceneOnFile) == MM_SCENE_HEADER_BYTES, "SceneOnFile on-disk layout drift");

const auto kbomScenh = 0x5FC00000;

#endif
