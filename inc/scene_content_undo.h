/* Scene caption undo facade extracted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef SCENE_CONTENT_UNDO_H
#define SCENE_CONTENT_UNDO_H
#include "three_mm/scene_content.h"
typedef class SceneUndoText *PSceneUndoText;

#define SceneUndoText_PAR MovieUndo
#define kclsSceneUndoText 'SUNX'
class SceneUndoText : public SceneUndoText_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    ThreeMMSceneTextUndo _content = {};
    SceneUndoText(void)
    {
    }

  public:
    static PSceneUndoText PsunxNew(void);
    ~SceneUndoText(void);

    void SetNfrmFirst(long nfrm)
    {
        _content.first = nfrm;
    }
    void SetNfrmLast(long nfrm)
    {
        _content.last = nfrm;
    }
    void SetItbox(long itbox)
    {
        _content.index = itbox;
    }
    void SetTbox(PTBOX ptbox)
    {
        _content.text = ptbox;
    }
    void SetAdd(bool fAdd)
    {
        _content.add = fAdd;
    }

    ThreeMMSceneTextUndo *CoreState()
    {
        return &_content;
    }
    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};
#endif
