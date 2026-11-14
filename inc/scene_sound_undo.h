/* Scene sound undo facade extracted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef SCENE_SOUND_UNDO_H
#define SCENE_SOUND_UNDO_H
#include "three_mm/scene_sound_edit.h"
typedef class SceneUndoSound *PSceneUndoSound;

#define SceneUndoSound_PAR MovieUndo
#define kclsSceneUndoSound 'SUNS'
class SceneUndoSound : public SceneUndoSound_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    ThreeMMSceneSoundUndo _sound;
    static ThreeMMSceneSoundValueServices _Values();
    ThreeMMSceneSoundUndoServices _Services();

    SceneUndoSound(void)
    {
        three_mm_scene_sound_undo_init(&_sound);
    }

  public:
    static PSceneUndoSound PsunsNew(void);
    ~SceneUndoSound(void);

    bool FSetSnd(PSceneSoundEvent psse)
    {
        auto values = _Values();
        return three_mm_scene_sound_undo_set(&_sound, psse, &values) != 0;
    }
    void SetSty(long sty)
    {
        _sound.style = sty;
    }
    ThreeMMSceneSoundUndo *CoreState()
    {
        return &_sound;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

#endif
