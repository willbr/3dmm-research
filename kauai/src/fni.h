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
#include "mm/filename_value.h"
#include "mm/filename_owned.h"
#include "mm/filename_enum.h"
#include "mm/filename_frame.h"

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

typedef LegacyLong FileType; // file type

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

    friend class FileObject;
    friend class FileNameEnumerator;

  private:
#ifdef UNICODE
    MMFilenameOwned16 _name;
#else
    MMFilenameOwned8 _name;
#endif

    void _SetFtgFromName(void);
    LegacyLong _CchExt(void);
    bool _FChangeLeaf(PString pstn);
    MMFilenameValue _ValueView();
    MMFilenameValue _StorageView();
    achar *_Path();
    void _SetRawPath(achar *);

  public:
    Filename(void);
    Filename(Filename &);
    Filename &operator=(Filename &);

// building FNIs
#ifdef MAC
    bool FGetOpen(FileType *prgftg, short cftg);
    bool FGetSave(FileType ftg, PPascalString pstPrompt, PPascalString pstDefault);
    bool FBuild(LegacyLong lwVol, LegacyLong lwDir, PString pstn, FileType ftg);
#elif defined(WIN) || defined(KAUAI_SDL3)
    // GUI dialogs are implemented by kauai (GUI); core-only callers use paths.
    bool FGetOpen(achar *prgchFilter, CoreNative::WindowHandle hwndOwner);
    bool FGetSave(achar *prgchFilter, CoreNative::WindowHandle hwndOwner);
#endif                                                   // WIN
    bool FSearchInPath(PString pstn, PString pstnEnv = pvNil);
    bool FBuildFromPath(PString pstn, FileType ftgDef = ftgNil);
    bool FGetUnique(FileType ftg);
    bool FGetTemp(void);
    void SetNil(void);

    FileType Ftg(void);
    LegacyUlong Grfvk(void); // volume kind (floppy/net/CD/etc)
    bool FChangeFtg(FileType ftg);

    bool FSetLeaf(PString pstn, FileType ftg = ftgNil);
    void GetLeaf(PString pstn);
    void GetStnPath(PString pstn);

    tribool TExists(void);
    // Read-only legacy content lookup: prefer exact spelling, otherwise accept
    // one unambiguous ASCII case match per component. Unchanged on failure.
    bool FResolveCase(void);
    bool FDelete(void);
    bool FRename(PFilename pfniNew);
    bool FEqual(PFilename pfni);

    bool FDir(void);
    bool FSameDir(PFilename pfni);
    bool FDownDir(PString pstn, LegacyUlong grffni);
    bool FUpDir(PString pstn, LegacyUlong grffni);
};

#ifdef MAC
#define FGetFniOpenMacro(pfni, prgftg, cftg, prgchFilter, hwndOwner) (pfni)->FGetOpen(prgftg, cftg)
#define FGetFniSaveMacro(pfni, ftg, pstPrompt, pstDef, prgchFilter, hwndOwner) (pfni)->FGetSave(ftg, pstPrompt, pstDef)
#endif // MAC
#if defined(WIN) || defined(KAUAI_SDL3)
#define FGetFniOpenMacro(pfni, prgftg, cftg, prgchFilter, hwndOwner) (pfni)->FGetOpen(prgchFilter, hwndOwner)
#define FGetFniSaveMacro(pfni, ftg, pstPrompt, pstDef, prgchFilter, hwndOwner) (pfni)->FGetSave(prgchFilter, hwndOwner)
#endif // WIN

/****************************************
    File name enumerator.
****************************************/
const LegacyLong kcftgFneBase = 20;

enum
{
    ffneNil = 0,
    ffneRecurse = 1,

    // for FNextFni
    ffnePre = 0x10,
    ffnePost = 0x20,
    ffneSkipDir = 0x80,
};

#define FileNameEnumerator_PAR BASE
#define kclsFileNameEnumerator 'FNE'
class FileNameEnumerator : public FileNameEnumerator_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(FileNameEnumerator)

  private:
    MMFilenameEnumState _enumeration;
#ifdef UNICODE
    MMFilenameFrame16 _fesCur;
#else
    MMFilenameFrame8 _fesCur;
#endif

    void _Free(void);
    MMFilenameEnumHost _EnumHost();
    MMFilenameEnumFrame _EnumFrame();
    MMFilenameMetadataView _NameMetadata(const MMFilenameValue &);

  public:
    FileNameEnumerator(void);
    ~FileNameEnumerator(void);

    bool FInit(Filename *pfniDir, FileType *prgftg, LegacyLong cftg, LegacyUlong grffne = ffneNil);
    bool FNextFni(Filename *pfni, LegacyUlong *pgrffneOut = pvNil, LegacyUlong grffneIn = ffneNil);
};

#endif //! FNI_H
