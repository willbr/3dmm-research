/* Scene chop undo declaration extracted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef SCENE_CHOP_UNDO_H
#define SCENE_CHOP_UNDO_H
typedef class SceneUndoChop *PSceneUndoChop;

#define SceneUndoChop_PAR MovieUndo
#define kclsSceneUndoChop 'SUNC'
class SceneUndoChop : public SceneUndoChop_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMSceneChopUndo _snapshot = {};
    SceneUndoChop(void)
    {
    }

  public:
    static PSceneUndoChop PsuncNew(void);
    ~SceneUndoChop(void);

    bool FSave(PScene pscen);

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

#endif
