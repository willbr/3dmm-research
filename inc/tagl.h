/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    tagl.h: Tag List class

    Primary Author: ******
    Review Status: REVIEWED - any changes to this file must be reviewed!

    BASE ---> TagList

***************************************************************************/
#ifndef TAGL_H
#define TAGL_H

/****************************************
    The tag list class
****************************************/
typedef class TagList *PTagList;
#define TagList_PAR BASE
#define kclsTagList 'TAGL'
class TagList : public TagList_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM

  protected:
    PGeneralGroup _pggtagf; // TAGF for fixed part, array of cc's for variable part

  protected:
    bool _FInit(void);
    bool _FFindTag(PTAG ptag, long *pitag);

  public:
    static PTagList PtaglNew(void);
    ~TagList(void);

    long Ctag(void);
    void GetTag(long itag, PTAG ptag);

    bool FInsertTag(PTAG ptag, bool fCacheChildren = fTrue);
    bool FInsertChild(PTAG ptag, ChildChunkID chid, ChunkTagOrType ctg);

    bool FCacheTags(void);
};

#endif TAGL_H
