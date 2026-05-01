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
    PMovie _pmvie;
    long _iscen;
    long _nfrm;

    MovieUndo(void)
    {
    }

  public:
    void SetPmvie(PMovie pmvie)
    {
        _pmvie = pmvie;
    }
    PMovie Pmvie(void)
    {
        return _pmvie;
    }

    void SetIscen(long iscen)
    {
        _iscen = iscen;
    }
    long Iscen(void)
    {
        return _iscen;
    }

    void SetNfrm(long nfrm)
    {
        _nfrm = nfrm;
    }
    long Nfrm(void)
    {
        return _nfrm;
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
    PActor _pactr;
    long _arid;
    bool _fSoonerLater;
    bool _fSndUndo;
    long _nfrmLast;
    String _stn; // actor's name
    ActorUndo(void)
    {
    }

  public:
    static PActorUndo PaundNew(void);
    ~ActorUndo(void);

    void SetPactr(PActor pactr);
    void SetArid(long arid)
    {
        _arid = arid;
    }
    void SetSoonerLater(bool fSoonerLater)
    {
        _fSoonerLater = fSoonerLater;
    }
    void SetSndUndo(bool fSndUndo)
    {
        _fSndUndo = fSndUndo;
    }
    void SetNfrmLast(long nfrmLast)
    {
        _nfrmLast = nfrmLast;
    }
    void SetStn(PString pstn)
    {
        _stn = *pstn;
    }

    bool FSoonerLater(void)
    {
        return _fSoonerLater;
    };
    bool FSndUndo(void)
    {
        return _fSndUndo;
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
    PDynamicArray _pglpaund; // List of PActorUndo. Owned (each child is AddRef'd).
    ActorMoveGroupUndo(void)
    {
    }

  public:
    static PActorMoveGroupUndo PamguNew(void);
    ~ActorMoveGroupUndo(void);

    bool FAddChild(PActorUndo paund); // Adds and AddRefs the child undo.
    long Cchild(void)
    {
        return _pglpaund == pvNil ? 0 : _pglpaund->IvMac();
    }

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
    PVirtualStringTable _pgstNames;
    ActorRenameGroupUndo(void)
    {
    }

  public:
    static PActorRenameGroupUndo PargNew(void);
    ~ActorRenameGroupUndo(void);

    // Records a rename: pstnPrev is the actor's pre-rename name, which is
    // what the first FUndo will restore. Mid-loop OOM returns fFalse.
    bool FAddChild(long arid, PString pstnPrev);

    long Cchild(void)
    {
        return _pgstNames == pvNil ? 0 : _pgstNames->IvMac();
    }

    virtual bool FDo(PDocumentBase pdocb);
    virtual bool FUndo(PDocumentBase pdocb);
};

//
// Definition of transition types
//
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

#endif // SOCUTIL_H
