/* Scene sound-packet host facade, extracted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef SCENE_SOUND_H
#define SCENE_SOUND_H
#include "mm/scene_sound.h"
/****************************************
    TagChildPair - Tag,Chid combo
    Runtime form: embeds full TAG with runtime pcrf. Allowed to grow on x64.
    The shared C packet codecs marshal the variable array to 20-byte wire records.
****************************************/
typedef struct TagChildPair *PTagChildPair;
struct TagChildPair
{
    ChildChunkID chid;
    TAG tag;
};

/****************************************
    SceneSoundEvent - scene sound event
****************************************/
typedef struct SceneSoundEvent *PSceneSoundEvent;
struct SceneSoundEvent : MMSceneSound
{
  protected:
    static long _Cb(long ctagc)
    {
        return mm_scene_sound_size(ctagc, size(TagChildPair));
    }
    SceneSoundEvent(void) {};

  public:
    static PSceneSoundEvent PsseNew(long ctagc);
    static PSceneSoundEvent PsseNew(long vlm, long sty, bool fLoop, long ctagc, TagChildPair *prgtagc);
    static PSceneSoundEvent PsseDupFromGg(PGeneralGroup pgg, long iv, bool fDupTags = fTrue);

    PTAG Ptag(long itagc)
    {
        PTagChildPair prgtagc = (PTagChildPair)PvAddBv(this, size(SceneSoundEvent));
        return &(prgtagc[itagc].tag);
    }
    PTagChildPair Ptagc(long itagc)
    {
        PTagChildPair prgtagc = (PTagChildPair)PvAddBv(this, size(SceneSoundEvent));
        return &(prgtagc[itagc]);
    }
    ChildChunkID *Pchid(long itagc)
    {
        PTagChildPair prgtagc = (PTagChildPair)PvAddBv(this, size(SceneSoundEvent));
        return &(prgtagc[itagc].chid);
    }
    PSceneSoundEvent PsseAddTagChid(PTAG ptag, long chid);
    PSceneSoundEvent PsseDup(void);
    void PlayAllSounds(PMovie pmvie, ulong dtsStart = 0);
    long Cb(void)
    {
        return _Cb(ctagc);
    }
};
static_assert(sizeof(SceneSoundEvent) == 16, "SceneSoundEvent on-disk fixed-part layout drift");
void ReleasePpsse(PSceneSoundEvent *ppsse);

PSceneSoundEvent SceneSoundFromWire(void *bytes, long byteCount, bool swap);
void *SceneSoundToWire(PSceneSoundEvent sound, long *bytes);
#endif
