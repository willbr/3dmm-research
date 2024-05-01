/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Command execution.  Manages the command filter list and dispatching
    commands to command handlers.

***************************************************************************/
#ifndef CMD_H
#define CMD_H

/***************************************************************************
    Command id, options and command struct
***************************************************************************/

// command handler forward declaration
class CommandHandler;
typedef CommandHandler *PCommandHandler;

// command enable-disable status flags
enum
{
    fedsNil = 0,
    fedsDisable = 1,
    fedsEnable = 2,
    fedsUncheck = 4,
    fedsCheck = 8,
    fedsBullet = 16
};
const ulong kgrfedsMark = fedsUncheck | fedsCheck | fedsBullet;

// command
#define kclwCmd 4 // if this ever changes, change the CMD_TYPE macro also
struct Command
{
    ASSERT

    PCommandHandler pcmh;          // the target of the command - may be nil
    long cid;           // the command id
    PGeneralGroup pgg;            // additional parameters for the command
    long rglw[kclwCmd]; // standard parameters
};
typedef Command *PCommand;

// command on file - for saving recorded macros
struct CommandFile
{
    long cid;
    long hid;
    long cact;
    ChildChunkID chidGg; // child id of the pgg, 0 if none
    long rglw[kclwCmd];
};

/***************************************************************************
    Custom command types
***************************************************************************/
// used to define a new Command structure.  Needs a trailing semicolon.
#define CMD_TYPE(foo, a, b, c, d)                                                                                      \
    struct CMD_##foo                                                                                                   \
    {                                                                                                                  \
        PCommandHandler pcmh;                                                                                                     \
        long cid;                                                                                                      \
        PGeneralGroup pgg;                                                                                                       \
        long a, b, c, d;                                                                                               \
    };                                                                                                                 \
    typedef CMD_##foo *PCMD_##foo

CMD_TYPE(KEY, ch, vk, grfcust, cact);   // defines CMD_KEY and PCMD_KEY
CMD_TYPE(BADKEY, ch, vk, grfcust, hid); // defines CMD_BADKEY and PCMD_BADKEY
CMD_TYPE(MOUSE, xp, yp, grfcust, cact); // defines CMD_MOUSE and PCMD_MOUSE

/***************************************************************************
    Command Map stuff.  To attach a command map to a subclass of CommandHandler,
    put a CMD_MAP_DEC(cls) in the definition of the class.  Then in the
    .cpp file, use BEGIN_CMD_MAP, ON_CID and END_CMD_MAP to define the
    command map.  This architecture was borrowed from MFC.
***************************************************************************/
enum
{
    fcmmNil = 0,
    fcmmThis = 1,
    fcmmNobody = 2,
    fcmmOthers = 4,
};
const ulong kgrfcmmAll = fcmmThis | fcmmNobody | fcmmOthers;

// for including a command map in this class
#define CMD_MAP_DEC(cls)                                                                                               \
  private:                                                                                                             \
    static CommandMapEntry _rgcmme##cls[];                                                                                        \
                                                                                                                       \
  protected:                                                                                                           \
    static CommandMap _cmm##cls;                                                                                              \
    virtual CommandMap *Pcmm(void)                                                                                            \
    {                                                                                                                  \
        return &_cmm##cls;                                                                                             \
    }

// for defining the command map in a .cpp file
#define BEGIN_CMD_MAP_BASE(cls)                                                                                        \
    cls::CommandMap cls::_cmm##cls = {pvNil, cls::_rgcmme##cls};                                                              \
    cls::CommandMapEntry cls::_rgcmme##cls[] = {
#define BEGIN_CMD_MAP(cls, clsBase)                                                                                    \
    cls::CommandMap cls::_cmm##cls = {&(clsBase::_cmm##clsBase), cls::_rgcmme##cls};                                          \
    cls::CommandMapEntry cls::_rgcmme##cls[] = {

#define ON_CID(cid, pfncmd, pfneds, grfcmm) {cid, (PFNCMD)pfncmd, (PFNEDS)pfneds, grfcmm},
#define ON_CID_ME(cid, pfncmd, pfneds) {cid, (PFNCMD)pfncmd, (PFNEDS)pfneds, fcmmThis},
#define ON_CID_GEN(cid, pfncmd, pfneds) {cid, (PFNCMD)pfncmd, (PFNEDS)pfneds, fcmmThis | fcmmNobody},
#define ON_CID_ALL(cid, pfncmd, pfneds) {cid, (PFNCMD)pfncmd, (PFNEDS)pfneds, kgrfcmmAll},

#define END_CMD_MAP(pfncmdDef, pfnedsDef, grfcmm)                                                                      \
    {                                                                                                                  \
        cidNil, (PFNCMD)pfncmdDef, (PFNEDS)pfnedsDef, grfcmm                                                           \
    }                                                                                                                  \
    }                                                                                                                  \
    ;
#define END_CMD_MAP_NIL()                                                                                              \
    {                                                                                                                  \
        cidNil, pvNil, pvNil, fcmmNil                                                                                  \
    }                                                                                                                  \
    }                                                                                                                  \
    ;

/***************************************************************************
    Command handler class
***************************************************************************/
#define CommandHandler_PAR BASE
#define kclsCommandHandler 'CMH'
class CommandHandler : public CommandHandler_PAR
{
    RTCLASS_DEC
    ASSERT

  private:
    static long _hidLast; // for HidUnique
    long _hid;            // handler id

  protected:
    // command function
    typedef bool (CommandHandler::*PFNCMD)(PCommand pcmd);

    // command enabler function
    typedef bool (CommandHandler::*PFNEDS)(PCommand pcmd, ulong *pgrfeds);

    // command map entry
    struct CommandMapEntry
    {
        long cid;
        PFNCMD pfncmd;
        PFNEDS pfneds;
        ulong grfcmm;
    };

    // command map
    struct CommandMap
    {
        CommandMap *pcmmBase;
        CommandMapEntry *prgcmme;
    };

    CMD_MAP_DEC(CommandHandler)

  protected:
    virtual bool _FGetCmme(long cid, ulong grfcmmWanted, CommandMapEntry *pcmme);

  public:
    CommandHandler(long hid);
    ~CommandHandler(void);

    // return indicates whether the command was handled, not success
    virtual bool FDoCmd(PCommand pcmd);
    virtual bool FEnableCmd(PCommand pcmd, ulong *pgrfeds);

    long Hid(void)
    {
        return _hid;
    }

    static long HidUnique(long ccmh = 1);
};

/***************************************************************************
    Command execution manager (dispatcher)
***************************************************************************/
// command stream recording error codes.
enum
{
    recNil,
    recFileError,
    recMemError,
    recWrongPlatform,
    recAbort,
    recLim
};

typedef class CommandExecutionManager *PCommandExecutionManager;
#define CommandExecutionManager_PAR BASE
#define kclsCommandExecutionManager 'CEX'
class CommandExecutionManager : public CommandExecutionManager_PAR
{
    RTCLASS_DEC
    ASSERT
    MARKMEM
    NOCOPY(CommandExecutionManager)

  protected:
    // an entry in the command handler list
    struct CMHE
    {
        PCommandHandler pcmh;
        long cmhl;
        ulong grfcmm;
    };

    // command recording/playback state
    enum
    {
        rsNormal,
        rsRecording,
        rsPlaying,
        rsLim
    };

    // recording and playback
    long _rs;       // recording/playback state
    long _rec;      // recording/playback errors
    PChunkyFile _pcfl;     // the file we are recording to or playing from
    PDynamicArray _pglcmdf;   // the command stream
    ChunkNumber _cno;       // which macro is being played
    long _icmdf;    // current command for recording or playback
    ChildChunkID _chidLast; // last chid used for recording
    long _cact;     // number of times on this command
    Command _cmd;       // previous command recorded or played

    // dispatching
    Command _cmdCur;     // command being dispatched
    long _icmheNext; // next command handler to dispatch to
    PGraphicsObject _pgobTrack; // the gob that is tracking the mouse
#ifdef WIN
    HWND _hwndCapture; // the hwnd that we captured the mouse with
#endif                 // WIN

    // filter list and command queue
    PDynamicArray _pglcmhe;       // the command filter list
    PDynamicArray _pglcmd;        // the command queue
    bool _fDispatching; // whether we're currently in FDispatchNextCmd

    // Modal filtering
    PGraphicsObject _pgobModal;

#ifdef DEBUG
    long _ccmdMax; // running max
#endif             // DEBUG

    CommandExecutionManager(void);

    virtual bool _FInit(long ccmdInit, long ccmhInit);
    virtual bool _FFindCmhl(long cmhl, long *picmhe);

    virtual bool _FCmhOk(PCommandHandler pcmh);
    virtual tribool _TGetNextCmd(void);
    virtual bool _FSendCmd(PCommandHandler pcmh);
    virtual void _CleanUpCmd(void);
    virtual bool _FEnableCmd(PCommandHandler pcmh, PCommand pcmd, ulong *pgrfeds);

    // command recording and playback
    bool _FReadCmd(PCommand pcmd);

  public:
    static PCommandExecutionManager PcexNew(long ccmdInit, long ccmhInit);
    ~CommandExecutionManager(void);

    // recording and play back
    bool FRecording(void)
    {
        return _rs == rsRecording;
    }
    bool FPlaying(void)
    {
        return _rs == rsPlaying;
    }
    void Record(PChunkyFile pcfl);
    void Play(PChunkyFile pcfl, ChunkNumber cno);
    void StopRecording(void);
    void StopPlaying(void);

    void RecordCmd(PCommand pcmd);

    // managing the filter list
    virtual bool FAddCmh(PCommandHandler pcmh, long cmhl, ulong grfcmm = fcmmNobody);
    virtual void RemoveCmh(PCommandHandler pcmh, long cmhl);
    virtual void BuryCmh(PCommandHandler pcmh);

    // queueing and dispatching
    virtual void EnqueueCmd(PCommand pcmd);
    virtual void PushCmd(PCommand pcmd);
    virtual void EnqueueCid(long cid, PCommandHandler pcmh = pvNil, PGeneralGroup pgg = pvNil, long lw0 = 0, long lw1 = 0, long lw2 = 0,
                            long lw3 = 0);
    virtual void PushCid(long cid, PCommandHandler pcmh = pvNil, PGeneralGroup pgg = pvNil, long lw0 = 0, long lw1 = 0, long lw2 = 0,
                         long lw3 = 0);
    virtual bool FDispatchNextCmd(void);
    virtual bool FGetNextKey(PCommand pcmd);
    virtual bool FCidIn(long cid);
    virtual void FlushCid(long cid);

    // menu marking
    virtual ulong GrfedsForCmd(PCommand pcmd);
    virtual ulong GrfedsForCid(long cid, PCommandHandler pcmh = pvNil, PGeneralGroup pgg = pvNil, long lw0 = 0, long lw1 = 0, long lw2 = 0,
                               long lw3 = 0);

    // mouse tracking
    virtual void TrackMouse(PGraphicsObject pgob);
    virtual void EndMouseTracking(void);
    virtual PGraphicsObject PgobTracking(void);

    virtual void Suspend(bool fSuspend = fTrue);
    virtual void SetModalGob(PGraphicsObject pgob);
};

#endif //! CMD_H
