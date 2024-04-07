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
