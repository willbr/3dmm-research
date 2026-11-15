/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    bkgd.h: Background class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> Background

***************************************************************************/
#ifndef BKGD_H
#define BKGD_H

#include <cstdint>

#include "background_data.h"
#include "three_mm/background_camera.h"
#include "three_mm/background_resource.h"

/****************************************
    Background Default Sound
****************************************/
// Authoring wire layout: 28 bytes with TAGOnFile. Runtime decoding uses
// three_mm_background_sound_read and never copies the stored pointer slot.
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

/****************************************
    The background class
****************************************/
typedef class Background *PBackground;
#define Background_PAR BaseCacheableObject
#define kclsBackground 'BKGD'
class Background : public Background_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    ThreeMMBackgroundResource _resource; // C-owned arrays, palette, sound and light policy.
    ThreeMMBackgroundCameraState _camera; // C-owned camera metadata/placement list.
    String _stn;            // name of this background

  protected:
    Background(void)
    {
        three_mm_background_camera_init(&_camera);
        three_mm_background_resource_init(&_resource);
    }
    ThreeMMBackgroundCameraServices _CameraServices(void *host);
    ThreeMMBackgroundServices _ResourceServices(void *host);
    bool _FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);

  public:
    static bool FAddTagsToTagl(PTAG ptagBkgd, PTagList ptagl);
    static bool FCacheToHD(PTAG ptagBkgd);
    static bool FReadBkgd(PChunkyResourceFile pcrf, ChunkTagOrType ctg, ChunkNumber cno, PDataBlock pblck, PBaseCacheableObject *ppbaco, LegacyLong *pcb);
    ~Background(void);
    void GetName(PString pstn);

    void TurnOnLights(PWorld pbwld);
    void TurnOffLights(void);
    bool FLeaveLitesOn(void)
    {
        return _resource.leave_on != 0;
    }
    void SetFLeaveLitesOn(bool fLeaveLitesOn)
    {
        _resource.leave_on = fLeaveLitesOn;
    }

    long Ccam(void)
    {
        return _camera.count;
    } // count of cameras in background
    long Icam(void)
    {
        return _camera.current;
    }                                        // currently selected camera
    bool FSetCamera(PWorld pbwld, long icam); // change camera to icam

    void GetMouseMatrix(BMAT34 *pbmat34);
    BRA BraRotYCamera(void)
    {
        return _camera.yaw;
    }
    void GetActorPlacePoint(BRS *pxr, BRS *pyr, BRS *pzr);
    void ReuseActorPlacePoint(void);

    void GetDefaultSound(PTAG ptagSnd, long *pvlm, bool *pfLoop)
    {
        TagFromCore(ptagSnd, _resource.sound.tag);
        *pvlm = _resource.sound.volume;
        *pfLoop = _resource.sound.loop != 0;
    }

    bool FGetPalette(PDynamicArray *ppglclr, long *piclrMin);
    void GetCameraPos(BRS *pxr, BRS *pyr, BRS *pzr);

#ifdef DEBUG
    // Authoring only.  Writes a special file with the given place info.
    bool FWritePlaceFile(BRS xrPlace, BRS yrPlace, BRS zrPlace);
#endif // DEBUG
};

#endif BKGD_H
