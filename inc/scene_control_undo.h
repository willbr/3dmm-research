/* Scene property undo declarations extracted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef SCENE_CONTROL_UNDO_H
#define SCENE_CONTROL_UNDO_H
typedef class SceneUndoTransition *PSceneUndoTransition;

#define SceneUndoTransition_PAR MovieUndo
#define kclsSceneUndoTransition 'SUNR'
class SceneUndoTransition : public SceneUndoTransition_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    ThreeMMSceneTransitionUndo _transition = {};
    SceneUndoTransition(void)
    {
    }

  public:
    static PSceneUndoTransition PsunrNew(void);
    ~SceneUndoTransition(void);

    void SetTrans(TRANS trans)
    {
        _transition.transition = trans;
    }
    ThreeMMSceneTransitionUndo *CoreState()
    {
        return &_transition;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

typedef class SceneUndoTitle *PSceneUndoTitle;

#define SceneUndoTitle_PAR MovieUndo
#define kclsSceneUndoTitle 'SUNT'
class SceneUndoTitle : public SceneUndoTitle_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    ThreeMMSceneTitleUndo _title;
    SceneUndoTitle(void)
    {
        three_mm_movie_title_init(&_title.name, sizeof(achar));
    }

  public:
    static PSceneUndoTitle PsuntNew(void);
    ~SceneUndoTitle(void);

    void SetName(PString pstn);
    ThreeMMSceneTitleUndo *CoreState()
    {
        return &_title;
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

#endif
