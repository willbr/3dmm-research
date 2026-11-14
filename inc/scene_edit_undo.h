/* Scene background/pause undo facades extracted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef SCENE_EDIT_UNDO_H
#define SCENE_EDIT_UNDO_H
#include "three_mm/scene_edit.h"
typedef class SceneUndoBackground *PSceneUndoBackground;

#define SceneUndoBackground_PAR MovieUndo
#define kclsSceneUndoBackground 'SUNK'
class SceneUndoBackground : public SceneUndoBackground_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    ThreeMMSceneBackgroundUndo _edit = {};
    SceneUndoBackground(void)
    {
    }

  public:
    static PSceneUndoBackground PsunkNew(void);
    ~SceneUndoBackground(void);

    void SetTag(PTAG ptag)
    {
        _edit.tag = {ptag->sid, ptag->pcrf, ptag->ctg, ptag->cno};
    }
    void SetIcam(long icam)
    {
        _edit.camera = icam;
    }
    void SetFBkgd(bool fSetBkgd)
    {
        _edit.background = fSetBkgd;
    }

    ThreeMMSceneBackgroundUndo *CoreState()
    {
        return &_edit;
    }
    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

typedef class SceneUndoPause *PSceneUndoPause;

#define SceneUndoPause_PAR MovieUndo
#define kclsSceneUndoPause 'SUNP'
class SceneUndoPause : public SceneUndoPause_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    ThreeMMScenePauseUndo _edit = {};
    SceneUndoPause(void)
    {
    }

  public:
    static PSceneUndoPause PsunpNew(void);
    ~SceneUndoPause(void);

    void SetWit(WaitReason wit)
    {
        _edit.reason = wit;
    }
    void SetDts(long dts)
    {
        _edit.delay = dts;
    }
    void SetAdd(bool fAdd)
    {
        _edit.add = fAdd;
    }

    ThreeMMScenePauseUndo *CoreState()
    {
        return &_edit;
    }
    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};
#endif
