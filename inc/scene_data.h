/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef SCENE_DATA_H
#define SCENE_DATA_H
#include "kauai_core.h"
#include <cstdint>
#include "three_mm/scene_data.h"

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
    sevtAddActr,    //    X		  		pactr/chid
    sevtPlaySnd,    // 	  		  X		SceneSoundEvent (Scene Sound Event)
    sevtAddTbox,    // 	  X				ptbox/chid
    sevtChngCamera, // 			  X		icam
    sevtSetBkgd,    // 	  X				Background Tag
    sevtPause       // 			  X		type, duration
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
static_assert(sizeof(SceneEvent) == 8, "SceneEvent on-disk layout drift");

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
static_assert(sizeof(SceneOnFile) == 16, "SceneOnFile on-disk layout drift");

const auto kbomScenh = 0x5FC00000;

// Scene state shared by persistence and playback. currentFrame is a runtime
// cursor; it is not stored in the scene header. Objects/audio live in Scene.
struct SceneData : ThreeMMSceneState
{
    SceneData() { three_mm_scene_init(this, sizeof(achar)); }
    void GetName(PString) const;
    void SetName(PString);
    void LoadName(PChunkyFile, ChunkNumber);
    bool FRead(PChunkyFile file, ChunkNumber number);
    bool FWrite(PChunkyFile file, ChunkNumber number);
    bool FCreate(PChunkyFile file, ChunkNumber *number);
    // Wire groups contain chunk IDs and fixed-width TAGs, never object pointers.
    static PGeneralGroup PggReadEvents(PDataBlock block, short *byteOrder, bool startEvents);
    static bool FWriteEvents(PChunkyFile file, ChunkNumber number, PGeneralGroup frames, PGeneralGroup starts);
};
#endif
