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

/****************************************
    Background Default Sound
****************************************/
// Runtime form of the background default sound. Holds a real TAG (with
// runtime pcrf pointer); never serialized directly. See
// BackgroundDefaultSoundOnFile below for the wire format.
struct BackgroundDefaultSound
{
    int16_t bo;
    int16_t osk;
    int32_t vlm;
    bool fLoop;
    TAG tagSnd;
};

// On-disk form: 28 bytes, TAGOnFile in place of TAG. Convert at the
// blck.FReadRgb / blck.FWrite boundary in bkgd.cpp.
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
    BACT *_prgbactLight; // array of light br_actors
    BLIT *_prgblitLight; // array of light data
    long _cbactLight;    // count of lights
    bool _fLites;        // lights are on
    bool _fLeaveLitesOn; // Don't turn out the lights
    long _ccam;          // count of cameras in this background
    long _icam;          // current camera
    BMAT34 _bmat34Mouse; // camera matrix for mouse model
    BRA _braRotY;        // Y rotation of current camera
    ChunkNumber _cnoSnd;         // background sound
    String _stn;            // name of this background
    PDynamicArray _pglclr;         // palette for this background
    byte _bIndexBase;    // first index for palette
    long _iaposLast;     // Last placement point we used
    long _iaposNext;     // Next placement point to use
    PDynamicArray _pglapos;        // actor placement point(s) for current view
    BRS _xrPlace;
    BRS _yrPlace;
    BRS _zrPlace;
    BackgroundDefaultSound _bds;   // background default sound
    BRS _xrCam; // camera position in worldspace
    BRS _yrCam;
    BRS _zrCam;

  protected:
    bool _FInit(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);
    long _Ccam(PChunkyFile pcfl, ChunkTagOrType ctg, ChunkNumber cno);
    void _SetupLights(PDynamicArray pgllite);

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
        return _fLeaveLitesOn;
    }
    void SetFLeaveLitesOn(bool fLeaveLitesOn)
    {
        _fLeaveLitesOn = fLeaveLitesOn;
    }

    long Ccam(void)
    {
        return _ccam;
    } // count of cameras in background
    long Icam(void)
    {
        return _icam;
    }                                        // currently selected camera
    bool FSetCamera(PWorld pbwld, long icam); // change camera to icam

    void GetMouseMatrix(BMAT34 *pbmat34);
    BRA BraRotYCamera(void)
    {
        return _braRotY;
    }
    void GetActorPlacePoint(BRS *pxr, BRS *pyr, BRS *pzr);
    void ReuseActorPlacePoint(void);

    void GetDefaultSound(PTAG ptagSnd, long *pvlm, bool *pfLoop)
    {
        *ptagSnd = _bds.tagSnd;
        *pvlm = _bds.vlm;
        *pfLoop = _bds.fLoop;
    }

    bool FGetPalette(PDynamicArray *ppglclr, long *piclrMin);
    void GetCameraPos(BRS *pxr, BRS *pyr, BRS *pzr);

#ifdef DEBUG
    // Authoring only.  Writes a special file with the given place info.
    bool FWritePlaceFile(BRS xrPlace, BRS yrPlace, BRS zrPlace);
#endif // DEBUG
};

#endif BKGD_H
