/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/*
 *
 * socutil.h
 *
 * This file contains miscellaneous includes and definitions
 * that are global to the Socrates product.
 *
 */

#ifndef SOCUTIL_H
#define SOCUTIL_H
#include "mm/actor_undo.h"
#include "mm/actor_group_undo.h"

extern "C"
{
#include "brender.h"
};

typedef class Actor *PActor;
typedef class Scene *PScene;
typedef class Movie *PMovie;
typedef class Background *PBackground;
typedef class TextBox *PTBOX;
typedef class Studio *PStudio;

//
//
// Class for undo items in a movie
//
// NOTE: All the "Set" functions are done automagically
// in Movie::FAddUndo().
//
//
typedef class MovieUndo *PMovieUndo;

#define MovieUndo_PAR UndoBase
#define kclsMovieUndo 'MUNB'
class MovieUndo : public MovieUndo_PAR
{
    RTCLASS_DEC
    ASSERT

  protected:
    MMMovieUndoContext _undoContext;

    MovieUndo(void)
    {
        mm_movie_undo_context_init(&_undoContext);
    }

  public:
    MMMovieUndoContext *CoreContext() { return &_undoContext; }
    void SetPmvie(PMovie pmvie)
    {
        _undoContext.movie = pmvie;
    }
    PMovie Pmvie(void) const
    {
        return static_cast<PMovie>(_undoContext.movie);
    }

    void SetIscen(long iscen)
    {
        _undoContext.scene = iscen;
    }
    long Iscen(void) const
    {
        return _undoContext.scene;
    }

    void SetNfrm(long nfrm)
    {
        _undoContext.frame = nfrm;
    }
    long Nfrm(void) const
    {
        return _undoContext.frame;
    }
};

//
// Undo object for actor operations
//
typedef class ActorUndo *PActorUndo;

#define ActorUndo_PAR MovieUndo
#define kclsActorUndo 'AUND'
class ActorUndo : public ActorUndo_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMActorUndoState _undoState;
    struct UndoContext;
    static MMActorUndoServices _UndoServices(UndoContext *);
    ActorUndo(void)
    {
        mm_actor_undo_init(&_undoState, &_undoContext, sizeof(achar));
    }

  public:
    static PActorUndo PaundNew(void);
    ~ActorUndo(void);

    void SetPactr(PActor pactr);
    void SetArid(long arid)
    {
        _undoState.actor_id = arid;
    }
    void SetSoonerLater(bool fSoonerLater)
    {
        _undoState.sooner_later = fSoonerLater;
    }
    void SetSndUndo(bool fSndUndo)
    {
        _undoState.sound = fSndUndo;
    }
    void SetNfrmLast(long nfrmLast)
    {
        _undoState.last_frame = nfrmLast;
    }
    void SetStn(PString pstn)
    {
        mm_actor_undo_set_name(&_undoState, pstn->Psz(), pstn->Cch());
    }

    bool FSoonerLater(void)
    {
        return _undoState.sooner_later != 0;
    };
    bool FSndUndo(void)
    {
        return _undoState.sound != 0;
    };

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

//
// Undo object for a single drag of the move tool that affects N selected actors.
// Owns N child ActorUndo snapshots and undoes/redoes them as a unit.
//
// Invariant: all children must come from the same scene and frame as the
// composite. FUndo propagates the composite's iscen/nfrm onto each child
// before invoking it; children's pre-existing scene/frame settings are
// overwritten. The move-tool drag pipeline guarantees this because a single
// drag is single-scene and single-frame.
//
typedef class ActorMoveGroupUndo *PActorMoveGroupUndo;

#define ActorMoveGroupUndo_PAR MovieUndo
#define kclsActorMoveGroupUndo 'AMGU'
class ActorMoveGroupUndo : public ActorMoveGroupUndo_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    MMActorGroupState _group; // Owns the child table and its references.
    PDynamicArray _Entries() const { return static_cast<PDynamicArray>(_group.entries); }
    static MMActorGroupServices _GroupServices(ActorMoveGroupUndo *);
    ActorMoveGroupUndo(void)
    {
        mm_actor_group_init(&_group, &_undoContext);
    }

  public:
    static PActorMoveGroupUndo PamguNew(void);
    ~ActorMoveGroupUndo(void);

    bool FAddChild(PActorUndo paund); // Adds and AddRefs the child undo.
    long Cchild(void);

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

//
// Composite undo for a batch of actor renames -- used by Ctrl+G / Ctrl+Shift+G
// to apply or strip a #tag across every selected actor as a single undo step.
//
// Each entry stores (arid, name-to-restore-on-toggle). FUndo and FDo both
// swap each actor's current name with the stored name, so undo/redo
// alternates between the pre- and post-rename states.
//
typedef class ActorRenameGroupUndo *PActorRenameGroupUndo;

#define ActorRenameGroupUndo_PAR MovieUndo
#define kclsActorRenameGroupUndo 'ARGU'
class ActorRenameGroupUndo : public ActorRenameGroupUndo_PAR
{
    RTCLASS_DEC
    MARKMEM
    ASSERT

  protected:
    // Each entry: GST string = "name to restore on the next FUndo/FDo",
    //             extra      = long arid.
    MMActorGroupState _group;
    PVirtualStringTable _Names() const { return static_cast<PVirtualStringTable>(_group.entries); }
    struct GroupContext;
    static MMActorGroupServices _GroupServices(GroupContext *);
    ActorRenameGroupUndo(void)
    {
        mm_actor_group_init(&_group, &_undoContext);
    }

  public:
    static PActorRenameGroupUndo PargNew(void);
    ~ActorRenameGroupUndo(void);

    // Records a rename: pstnPrev is the actor's pre-rename name, which is
    // what the first FUndo will restore. Mid-loop OOM returns fFalse.
    bool FAddChild(long arid, PString pstnPrev);

    long Cchild(void);

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

//
// Definition of transition types
//
#include "scene_data.h"

#endif // SOCUTIL_H
