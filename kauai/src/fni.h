/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    File name handling.

***************************************************************************/
#ifndef FNI_H
#define FNI_H

using Group::PDynamicArray;

#ifdef MAC
typedef FSSpec FSS;
#endif // MAC

enum
{
    ffniNil = 0x0000,
    ffniCreateDir = 0x0001,
    ffniMoveToDir = 0x0002,

// for Filename::AssertValid
#ifdef DEBUG
    ffniFile = 0x10000,
    ffniDir = 0x20000,
    ffniEmpty = 0x40000,
#endif
};

// Volume kinds:
enum
{
    fvkNil = 0x0000,
    fvkFloppy = 0x0001,
    fvkNetwork = 0x0002,
    fvkCD = 0x0004,
    fvkRemovable = 0x0008,
};

typedef long FileType; // file type

const FileType ftgNil = '...,';
const FileType kftgDir = '....';
const FileType kftgTemp = MacWin('temp', 'TMP'); // the standard temp file ftg
const FileType kftgText = MacWin('TEXT', 'TXT');

extern FileType vftgTemp; // the ftg to use for temp files

/****************************************
    File name class
****************************************/
typedef class Filename *PFilename;
#define Filename_PAR BASE
#define kclsFilename 'FNI'
class Filename : public Filename_PAR
{
    RTCLASS_DEC
    ASSERT

    friend class FIL;
    friend class FNE;

  private:
    FileType _ftg;
#ifdef MAC
    long _lwDir; // the directory id
    FSS _fss;
#elif defined(WIN)
    STN _stnFile;
#endif // WIN

#ifdef WIN
    void _SetFtgFromName(void);
    long _CchExt(void);
    bool _FChangeLeaf(PSTN pstn);
#endif // WIN

  public:
    Filename(void);

// building FNIs
#ifdef MAC
    bool FGetOpen(FileType *prgftg, short cftg);
    bool FGetSave(FileType ftg, PST pstPrompt, PST pstDefault);
    bool FBuild(long lwVol, long lwDir, PSTN pstn, FileType ftg);
#elif defined(WIN)
    bool FGetOpen(achar *prgchFilter, HWND hwndOwner);
    bool FGetSave(achar *prgchFilter, HWND hwndOwner);
    bool FSearchInPath(PSTN pstn, PSTN pstnEnv = pvNil);
#endif                                                   // WIN
    bool FBuildFromPath(PSTN pstn, FileType ftgDef = ftgNil); // REVIEW shonk: Mac: implement
    bool FGetUnique(FileType ftg);
    bool FGetTemp(void);
    void SetNil(void);

    FileType Ftg(void);
    ulong Grfvk(void); // volume kind (floppy/net/CD/etc)
    bool FChangeFtg(FileType ftg);

    bool FSetLeaf(PSTN pstn, FileType ftg = ftgNil);
    void GetLeaf(PSTN pstn);
    void GetStnPath(PSTN pstn);

    tribool TExists(void);
    bool FDelete(void);
    bool FRename(PFilename pfniNew);
    bool FEqual(PFilename pfni);

    bool FDir(void);
    bool FSameDir(PFilename pfni);
    bool FDownDir(PSTN pstn, ulong grffni);
    bool FUpDir(PSTN pstn, ulong grffni);
};

#ifdef MAC
#define FGetFniOpenMacro(pfni, prgftg, cftg, prgchFilter, hwndOwner) (pfni)->FGetOpen(prgftg, cftg)
#define FGetFniSaveMacro(pfni, ftg, pstPrompt, pstDef, prgchFilter, hwndOwner) (pfni)->FGetSave(ftg, pstPrompt, pstDef)
#endif // MAC
#ifdef WIN
#define FGetFniOpenMacro(pfni, prgftg, cftg, prgchFilter, hwndOwner) (pfni)->FGetOpen(prgchFilter, hwndOwner)
#define FGetFniSaveMacro(pfni, ftg, pstPrompt, pstDef, prgchFilter, hwndOwner) (pfni)->FGetSave(prgchFilter, hwndOwner)
#endif // WIN

/****************************************
    File name enumerator.
****************************************/
const long kcftgFneBase = 20;

enum
{
    ffneNil = 0,
    ffneRecurse = 1,

    // for FNextFni
    ffnePre = 0x10,
    ffnePost = 0x20,
    ffneSkipDir = 0x80,
};

#define FNE_PAR BASE
#define kclsFNE 'FNE'
class FNE : public FNE_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(FNE)

  private:
    // file enumeration state
    struct FES
    {
#ifdef MAC
        long lwVol;
        long lwDir;
        long iv;
#endif // MAC
#ifdef WIN
        Filename fni; // directory fni
        HN hn;   // for enumerating files/directories
        WIN32_FIND_DATA wfd;
        ulong grfvol; // which volumes are available (for enumerating volumes)
        long chVol;   // which volume we're on (for enumerating volumes)
#endif                // WIN
    };

    FileType _rgftg[kcftgFneBase];
    FileType *_prgftg;
    long _cftg;
    bool _fRecurse : 1;
    bool _fInited : 1;
    PDynamicArray _pglfes;
    FES _fesCur;

    void _Free(void);
#ifdef WIN
    bool _FPop(void);
#endif // WIN

  public:
    FNE(void);
    ~FNE(void);

    bool FInit(Filename *pfniDir, FileType *prgftg, long cftg, ulong grffne = ffneNil);
    bool FNextFni(Filename *pfni, ulong *pgrffneOut = pvNil, ulong grffneIn = ffneNil);
};

#endif //! FNI_H
