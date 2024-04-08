/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tatr.h: Theater class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> BaseCacheableObject ---> CommandHandler ---> Theater

***************************************************************************/
#ifndef TATR_H
#define TATR_H

#ifdef DEBUG // Flags for Theater::AssertValid()
enum
{
    ftatrNil = 0x0000,
    ftatrMvie = 0x0001,
};
#endif // DEBUG

/****************************************
    The theater class
****************************************/
typedef class Theater *PTheater;
#define Theater_PAR CommandHandler
#define kclsTheater 'TATR'
class Theater : public Theater_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    CMD_MAP_DEC(Theater)

  protected:
    long _kidParent; // ID of gob parent of MovieView
    PMovie _pmvie;    // Currently loaded movie

  protected:
    Theater(long hid) : CommandHandler(hid)
    {
    }
    bool _FInit(long kidParent);

  public:
    static PTheater PtatrNew(long kidParent);
    ~Theater(void);

    bool FCmdLoad(PCommand pcmd);
    bool FCmdPlay(PCommand pcmd);
    bool FCmdStop(PCommand pcmd);
    bool FCmdRewind(PCommand pcmd);

    PMovie Pmvie(void)
    {
        return _pmvie;
    }
};

#endif TATR_H
