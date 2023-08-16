/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

//
//
//
// Studio control code
//
// Created: Jan, 10, 1995
//
//
//

#include "studio.h"

ASSERTNAME
RTCLASS(Studio)
RTCLASS(StudioClientCallbacks)

BEGIN_CMD_MAP(Studio, CMH)
ON_CID_GEN(cidNew, &Studio::FCmdOpen, pvNil)
ON_CID_GEN(cidOpen, &Studio::FCmdOpen, pvNil)
ON_CID_GEN(cidXYAxis, &Studio::FCmdXYAxis, pvNil)
ON_CID_GEN(cidXZAxis, &Studio::FCmdXZAxis, pvNil)
ON_CID_GEN(cidSetTool, &Studio::FCmdSetTool, pvNil)
ON_CID_GEN(cidPlay, &Studio::FCmdPlay, pvNil)
ON_CID_GEN(cidNewScene, &Studio::FCmdNewScene, pvNil)
ON_CID_GEN(cidRespectGround, &Studio::FCmdRespectGround, pvNil)
ON_CID_GEN(cidPauseUntilClick, &Studio::FCmdPause, pvNil)
ON_CID_GEN(cidPauseForSound, &Studio::FCmdPause, pvNil)
ON_CID_GEN(cidClearPause, &Studio::FCmdPause, pvNil)
ON_CID_GEN(cidBrowserReady, &Studio::FCmdBrowserReady, pvNil)
ON_CID_GEN(cidFrameScrollbar, &Studio::FCmdScroll, pvNil)
ON_CID_GEN(cidFrameThumb, &Studio::FCmdScroll, pvNil)
ON_CID_GEN(cidSceneScrollbar, &Studio::FCmdScroll, pvNil)
ON_CID_GEN(cidSceneThumb, &Studio::FCmdScroll, pvNil)
ON_CID_GEN(cidStartScroll, &Studio::FCmdScroll, pvNil)
ON_CID_GEN(cidEndScroll, &Studio::FCmdScroll, pvNil)
ON_CID_GEN(cidSooner, &Studio::FCmdSooner, pvNil)
ON_CID_GEN(cidLater, &Studio::FCmdLater, pvNil)
ON_CID_GEN(cidNewSpletter, &Studio::FCmdNewSpletter, pvNil)
ON_CID_GEN(cidTextBkgdColor, &Studio::FCmdCreatePopup, pvNil)
ON_CID_GEN(cidTextColor, &Studio::FCmdCreatePopup, pvNil)
ON_CID_GEN(cidTextFont, &Studio::FCmdCreatePopup, pvNil)
ON_CID_GEN(cidTextSize, &Studio::FCmdCreatePopup, pvNil)
ON_CID_GEN(cidTextStyle, &Studio::FCmdCreatePopup, pvNil)
ON_CID_GEN(cidTextSetColor, &Studio::FCmdTextSetColor, pvNil)
ON_CID_GEN(cidTextSetBkgdColor, &Studio::FCmdTextSetBkgdColor, pvNil)
ON_CID_GEN(cidTextSetFont, &Studio::FCmdTextSetFont, pvNil)
ON_CID_GEN(cidTextSetSize, &Studio::FCmdTextSetSize, pvNil)
ON_CID_GEN(cidTextSetStyle, &Studio::FCmdTextSetStyle, pvNil)
ON_CID_GEN(cidOpenSoundRecord, &Studio::FCmdOpenSoundRecord, pvNil)
ON_CID_GEN(cidToggleXY, &Studio::FCmdToggleXY, pvNil)
ON_CID_GEN(cidHelpBook, &Studio::FCmdHelpBook, pvNil)
ON_CID_GEN(cidMovieGoto, &Studio::FCmdMovieGoto, pvNil)
ON_CID_GEN(cidLoadProjectMovie, &Studio::FCmdLoadProjectMovie, pvNil)
ON_CID_GEN(cidSoundsEnabled, &Studio::FCmdSoundsEnabled, pvNil)
ON_CID_GEN(cidCreateTbox, &Studio::FCmdCreateTbox, pvNil)
ON_CID_GEN(cidActorEaselOpen, &Studio::FCmdActorEaselOpen, pvNil)
ON_CID_GEN(cidListenerEaselOpen, &Studio::FCmdListenerEaselOpen, pvNil)
#ifdef DEBUG
ON_CID_GEN(cidWriteBmps, &Studio::FCmdWriteBmps, pvNil)
#endif // DEBUG
END_CMD_MAP_NIL()

const long kcbCursorCache = 1024;

/***************************************************************************
 *
 * Create the studio and get it running.
 *
 * Parameters:
 *  hid - The hid to use for the studio
 *  pcrmStudio - ChunkyResourceManager to read script chunks from
 *  pfniUserDoc - movie file to open, or pvNil
 *  fFailIfDocOpenFailed - if fTrue, this function fails if pfniUserDoc
 *     cannot be opened.  If fFalse, this function creates a blank document
 *     and does not fail if pfniUserDoc cannot be opened.
 *
 * Returns:
 *  Pointer to the studio if successful, else pvNil.
 *
 **************************************************************************/
PStudio Studio::PstdioNew(long hid, PChunkyResourceManager pcrmStudio, PFilename pfniUserDoc, bool fFailIfDocOpenFailed)
{
    AssertPo(pcrmStudio, 0);
    AssertNilOrPo(pfniUserDoc, ffniFile);
    AssertPo(((APP *)vpappb)->Pkwa(), 0);

    bool fSuccess = fFalse;
    PStudio pstdio;
    PMovie pmvie = pvNil;
    GraphicsObjectBlock gcb;
    DataBlock blck;

    gcb.Set(hid, ((APP *)vpappb)->Pkwa());
    pstdio = NewObj Studio(&gcb);

    if (pstdio == pvNil)
    {
        return (pvNil);
    }

    pstdio->_pcrm = pcrmStudio;
    pstdio->_pcrm->AddRef();

    pstdio->_psmcc = NewObj StudioClientCallbacks(kdxpWorkspace, kdypWorkspace, kcbStudioCache, pvNil, pstdio);

    if (pstdio->_psmcc == pvNil)
    {
        goto LFail;
    }

    pmvie = vapp.PmvieRetrieve();

    // _FOpenStudio() depends on _psmcc being initialized
    if (!pstdio->_FOpenStudio((pfniUserDoc == pvNil) && (pmvie == pvNil)))
    {
        goto LFail;
    }

    if (pmvie != pvNil)
    {
        pmvie->SetMcc(pstdio->_psmcc);
        if (!pstdio->FSetMovie(pmvie))
            goto LFail;
        pmvie->ResetTitle();
    }
    else if (!pstdio->FLoadMovie(pfniUserDoc))
    {
        if (fFailIfDocOpenFailed)
            goto LFail;
        else if (!pstdio->FLoadMovie()) // try blank doc
            goto LFail;
    }

    AssertPo(pstdio, 0);

    fSuccess = fTrue;

LFail:
    ReleasePpo(&pmvie);
    if (!fSuccess)
        ReleasePpo(&pstdio);
    return (pstdio);
}

/***************************************************************************
 *
 * Destroy the studio.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
Studio::~Studio(void)
{
    ReleasePpo(&_pmvie);
    ReleasePpo(&_psmcc);
    ReleasePpo(&_pcrm);
    ReleasePpo(&_pgstMisc);
    ReleasePpo(&_pbrwrActr);
    ReleasePpo(&_pbrwrProp);
    ReleasePpo(&_pglcmg);
    ReleasePpo(&_pglclr);
    ReleaseBrcn();

    PGraphicsObject pgobStudio = ((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);
    ReleasePpo(&pgobStudio);
}

/***************************************************************************
 *
 * Open studio.chk and start the studio script
 *
 * Parameters:
 *  fPaletteFade - fTrue if we should do a palette fade.
 *
 * Returns:
 *  fTrue if successful, else fFalse.
 *
 **************************************************************************/
bool Studio::_FOpenStudio(bool fPaletteFade)
{
    AssertBaseThis(0);

    PScript pscpt = pvNil;
    PStudioScrollbars psscb = pvNil;
    PGraphicsObjectInterpreter psceg = pvNil;
    STN stn;
    RC rcAbs, rcRel;
    DataBlock blck;
    bool fRet = fFalse;
    long icrf;
    PChunkyResourceFile pcrf;
    long lwParm;

    vapp.BeginLongOp();

    // read miscellaneous message strings
    for (icrf = 0; icrf < _pcrm->Ccrf(); icrf++)
    {
        pcrf = _pcrm->PcrfGet(icrf);
        if (pcrf->Pcfl()->FFind(kctgGst, kcnoGstMisc, &blck))
        {
            _pgstMisc = StringTable::PgstRead(&blck);
            break;
        }
    }
    if (pvNil == _pgstMisc) // if not found or read error
        goto LFail;

    if (pvNil == (psceg = ((APP *)vpappb)->Pkwa()->PscegNew(_pcrm, ((APP *)vpappb)->Pkwa())))
    {
        goto LFail;
    }

    //
    // Read in common palette
    //
    for (icrf = 0; icrf < _pcrm->Ccrf(); icrf++)
    {
        pcrf = _pcrm->PcrfGet(icrf);
        if (pcrf->Pcfl()->FFind(kctgColorTable, kidPalette, &blck))
        {
            _pglclr = DynamicArray::PglRead(&blck);
            if (_pglclr != pvNil)
            {
                break;
            }
        }
    }

    // kidStudio should be kcnoStudio according to Hungarian, but the "kid"
    // prefix is entrenched into the script/help stuff and can't be easily
    // all changed to kcno.
    if (pvNil == (pscpt = (PScript)_pcrm->PbacoFetch(kctgScript, kidStudio, Script::FReadScript)))
    {
        goto LFail;
    }

    lwParm = !fPaletteFade;

    if (!psceg->FRunScript(pscpt, &lwParm, 1))
    {
        vpappb->TGiveAlertSz("Running script failed", bkOk, cokExclamation);
        goto LFail;
    }

    // Create the scroll bars
    if (pvNil == (psscb = StudioScrollbars::PsscbNew(_pmvie)))
    {
        goto LFail;
    }
    AssertVarMem(_psmcc);
    _psmcc->SetSscb(psscb);

    if (!vpcex->FAddCmh(this, kcmhlStudio))
        goto LFail;

    fRet = fTrue;

LFail:

    Assert(fRet, "Warning: Failed to open studio file..."
                 "do you have studio.chk in the working directory?");

    ReleasePpo(&psscb);
    ReleasePpo(&pscpt);
    ReleasePpo(&psceg);
    ReleasePpo(&_pbrwrActr);
    ReleasePpo(&_pbrwrProp);
    ReleasePpo(&_pglcmg);

    vapp.EndLongOp();

    return fRet;
}

/***************************************************************************
    Set the portfolio doc from one of Melanie's project documents
***************************************************************************/
bool Studio::FCmdLoadProjectMovie(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    long stid = pcmd->rglw[0];
    STN stn;
    STN stnLeaf;
    Filename fni;
    bool fLoaded = fFalse;

    if (!vpapp->Pkwa()->Pstrg()->FGet(stid, &stnLeaf))
    {
        Bug("Missing string in FCmdLoadProjectMovie");
        goto LEnd;
    }
    vpapp->GetFniMelanie(&fni); // Get Melanie's directory
    if (!fni.FSetLeaf(&stnLeaf))
        goto LEnd;
    if (tYes != fni.TExists())
    {
        PushErc(ercSocMissingMelanieDoc);
        goto LEnd;
    }
    if (!FLoadMovie(&fni))
    {
        PushErc(ercSocCantLoadMelanieDoc);
        goto LEnd;
    }
    Assert(_pmvie != pvNil, "FLoadMovie lied to us");

    /* Make sure the autosave file's been switched to a temp file */
    if (!_pmvie->FEnsureAutosave())
        goto LEnd;

    /* Tell the movie to forget about the original file */
    _pmvie->ForceSaveAs();

    fLoaded = fTrue;
LEnd:
    vpcex->EnqueueCid(cidProjectMovieLoaded, pvNil, pvNil, fLoaded);
    return fTrue;
}

/***************************************************************************
 *
 * Change the current movie -- close any currently open movie.
 *
 * Parameters:
 *  pfni - File to read from.
 *  cno - Cno within the file.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FLoadMovie(PFilename pfni, ChunkNumber cno)
#ifdef BUG1959
{
    bool fRet, fClosedOld;

    /* If loading a specific movie, attempt to load a blank movie first */
    if (pfni != pvNil)
    {
        if (!(fRet = _FLoadMovie(pvNil, cnoNil, &fClosedOld)) || !fClosedOld)
            goto LDone;
    }

    /* Now do what the user asked us to do */
    fRet = _FLoadMovie(pfni, cno, &fClosedOld);

LDone:
    return fRet;
}

bool Studio::_FLoadMovie(PFilename pfni, ChunkNumber cno, bool *pfClosedOld)
#endif // BUG1959
{
    AssertBaseThis(0);
    AssertNilOrPo(pfni, 0);

    bool fRet = fFalse;
    PMovieView pmvu;
    PMovie pmvie = pvNil, pmvieOld = pvNil;
    PBackground pbkgd = pvNil;

#ifdef BUG1959
    *pfClosedOld = fTrue;
#endif // BUG1959

    if (_pmvie != pvNil)
    {
        PScene pscen;

        pmvu = (PMovieView)_pmvie->PddgActive();
        AssertPo(pmvu, 0);

        if (!pmvu->FCloseDoc(fFalse, fTrue))
        {
            fRet = fTrue;
#ifdef BUG1959
            *pfClosedOld = fFalse;
#endif // BUG1959
            goto LFail;
        }
        _pmvie->Flush();
        pmvieOld = _pmvie;
        pmvieOld->AddRef();
        if ((pscen = pmvieOld->Pscen()) != pvNil)
        {
            if ((pbkgd = pscen->Pbkgd()) != pvNil)
                pbkgd->SetFLeaveLitesOn(fTrue);
        }
    }

    pmvie = Movie::PmvieNew(vpapp->FSlowCPU(), _psmcc, pfni, cno);
    if (pmvie == pvNil)
    {
        ReleasePpo(&pmvieOld);
        goto LFail;
    }

    fRet = FSetMovie(pmvie);

    if (fRet)
    {
        PKidspaceGraphicObject pgok;

        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);

        if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject) && (_pmvie->Pscen() != pvNil))
        {
            AssertPo(pgok, 0);
            pgok->FRunScript((kstDefault << 16) | kchidOpenDoorsAll);
            pgok->FRunScript((kstDefault << 16) | kchidPopoutSceneTools);
        }
    }

LFail:
    ReleasePpo(&pmvie);

    if (pbkgd != pvNil)
        pbkgd->SetFLeaveLitesOn(fFalse);

    /* Restore old movie if loading new one failed */
    if (_pmvie == pvNil)
    {
        Assert(!fRet, "Bogus state for studio");
        _pmvie = pmvieOld;
        Psmcc()->UpdateRollCall();
    }
    else if (pmvieOld != pvNil && fRet)
    {
        bool fResetLites = (_pmvie->Pscen() != pvNil && _pmvie->Pscen()->Pbkgd() == pbkgd);

        Assert(!fResetLites || pbkgd->CactRef() > 1, "Not enough refs for Background");
        pmvieOld->CloseAllDdg();
        ReleasePpo(&pmvieOld);

        /* Turn lights back on, in new World (they got turned off when releasing
            the old movie) */
        if (fResetLites)
            pbkgd->TurnOnLights(_pmvie->Pbwld());
    }

#ifdef BUG1959
    if (pfni == pvNil && fRet && *pfClosedOld)
        vptagm->ClearCache(sidNil, ftagmFile);
#endif // BUG1959

    return fRet;
}

/***************************************************************************
 *
 * Set the current movie -- close any currently open movie.
 *
 * Parameters:
 *  pmvie - The new movie.
 *
 * Returns:
 *  fTrue if it was succesful, else fFalse.
 *
 **************************************************************************/
bool Studio::FSetMovie(PMovie pmvie)
{
    AssertPo(pmvie, 0);

    bool fRet = fFalse;
    PMovieView pmvu = pvNil;
    RC rcRel, rcAbs;
    PKidspaceGraphicObject pgok;
    GraphicsObjectBlock gcb;

    vapp.BeginLongOp();

    ReleasePpo(&_pmvie);
    _psmcc->Psscb()->SetMvie(pvNil);
    _pmvie = pmvie;
    _pmvie->AddRef();

    //
    // Position the view
    //
    rcRel.Set(krelZero, krelZero, krelOne, krelOne);
    rcAbs.Set(0, 0, 0, 0);

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidWorkspace);
    AssertPo(pgok, 0);

    if (pvNil == _psmcc->Psscb())
        goto LFail;

    //
    // Create the view
    //
    gcb.Set(khidDdg, pgok, fgobNil, kginDefault, &rcAbs, &rcRel);
    pmvu = (PMovieView)_pmvie->PddgNew(&gcb);
    if (pmvu == pvNil)
        goto LFail;
    AssertPo(pmvu, 0);

    // Set the movie title
    UpdateTitle(_pmvie->PstnTitle());

    _pmvie->SetThumbPalette(_pglclr);

    if (_pmvie->Cscen() > 0)
    {
        if (!_pmvie->FSwitchScen(0))
        {
            goto LFail;
        }

        EnableActorTools();
        EnableTboxTools();
    }

    _psmcc->Psscb()->SetMvie(_pmvie);
    pmvu->SetTool(toolDefault);
    _psmcc->UpdateRollCall();

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidSettingsCover);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        pgok->FChangeState(kstOpen);
    }

    _SetToolStates();

    fRet = fTrue;
LFail:
    if (!fRet)
    {
        ReleasePpo(&_ptgobTitle);
        ReleasePpo(&pmvu);
        ReleasePpo(&_pmvie);
    }
    vapp.EndLongOp();
    return fRet;
}

/***************************************************************************
 *
 * Handle XYAxis command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdXYAxis(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovieView pmvu;
    BRS rgr[3][3] = {{rOne, rZero, rZero}, {rZero, rOne, rZero}, {rZero, rZero, -rOne}};

    if (pvNil != _pmvie)
    {
        AssertPo(_pmvie, 0);
        pmvu = (PMovieView)_pmvie->PddgActive();
        pmvu->SetAxis(rgr);

        if ((pmvu->Tool() != toolCompose) && (pmvu->Tool() != toolRecordSameAction) && (pmvu->Tool() != toolAction) &&
            (pmvu->Tool() != toolPlace) && !pmvu->FTextMode())
        {
            pmvu->SetTool(toolCompose);
            ChangeTool(toolCompose);
        }
    }

    return fTrue;
}

/***************************************************************************
 *
 * Handle XZAxis command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdXZAxis(PCMD pcmd)
{
    AssertThis(0);
    PMovieView pmvu;
    BRS rgr[3][3] = {{rOne, rZero, rZero}, {rZero, rZero, rOne}, {rZero, -rOne, rZero}};

    if (pvNil != _pmvie)
    {
        AssertPo(_pmvie, 0);
        pmvu = (PMovieView)_pmvie->PddgActive();
        pmvu->SetAxis(rgr);

        if ((pmvu->Tool() != toolCompose) && (pmvu->Tool() != toolRecordSameAction) && (pmvu->Tool() != toolAction) &&
            (pmvu->Tool() != toolPlace) && !pmvu->FTextMode())
        {
            pmvu->SetTool(toolCompose);
            ChangeTool(toolCompose);
        }
    }

    return fTrue;
}

/***************************************************************************
 *
 * Handle changing the current tool.
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdSetTool(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovieView pmvu;

    if (pvNil != _pmvie)
    {
        AssertPo(_pmvie, 0);
        pmvu = (PMovieView)_pmvie->PddgActive();

        switch (pcmd->rglw[0])
        {
        case chttCompose:
            if (pmvu->Tool() != toolPlace)
            {
                pmvu->SetTool(toolCompose);
            }
            break;

        case chttNormalizeRot:
            pmvu->SetTool(toolNormalizeRot);
            break;

        case chttNormalizeSize:
            pmvu->SetTool(toolNormalizeSize);
            break;

        case chttSooner:
            pmvu->SetTool(toolSoonerLater);
            break;

        case chttRotateX:
            pmvu->SetTool(toolRotateX);
            break;

        case chttRotateY:
            pmvu->SetTool(toolRotateY);
            break;

        case chttRotateZ:
            pmvu->SetTool(toolRotateZ);
            break;

        case chttRotateNorm:
            pmvu->SetTool(toolNormalizeRot);
            break;

        case chttSquash:
            pmvu->SetTool(toolSquashStretch);
            break;

        case chttShrink:
            pmvu->SetTool(toolResize);
            break;

        case chttTransformNorm:
            pmvu->SetTool(toolNormalizeSize);
            break;

        case chttRecordSameAction:
            pmvu->SetTool(toolRecordSameAction);
            break;

        case chttAction:
            _cmd = *pcmd;
            pmvu->SetTool(toolAction);
            break;

        case chttTboxSelect:
            pmvu->SetTool(toolTboxMove);
            break;

        case chttTboxStory:
            pmvu->SetTool(toolTboxStory);
            break;

        case chttTboxScroll:
            pmvu->SetTool(toolTboxCredit);
            break;

        case chttSceneChopFwd:
            pmvu->SetTool(toolSceneChop);
            break;

        case chttSceneChopBack:
            pmvu->SetTool(toolSceneChopBack);
            break;

        case chttSceneNuke:
            pmvu->SetTool(toolSceneNuke);
            break;

        case chttActorNuke:
            pmvu->SetTool(toolActorNuke);
            break;

        case chttActorEasel:
            pmvu->SetTool(toolActorEasel);
            break;

        case chttMatcher:
            pmvu->SetTool(toolMatcher);
            break;

        case chttSounder:
            pmvu->SetTool(toolSounder);
            break;

        case chttLooper:
            pmvu->SetTool(toolLooper);
            break;

        case chttListener:
            pmvu->SetTool(toolListener);
            break;

        case chttNone:
            pmvu->SetTool(toolDefault);
            break;

        default:
            Bug("You forgot to put this tool in SetTool.");
            break;
        }
    }

    return fTrue;
}

/***************************************************************************
 *
 * Handle Play command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdPlay(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PKidspaceGraphicObject pgok;

    if (pvNil != _pmvie)
    {
        AssertPo(_pmvie, 0);

        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsActionBrowser);

        if (_pmvie->FPlaying())
        {

            vpappb->FSetProp(kpridToolTipDelay, _dtimToolTipDelay);
            if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
            {
                AssertPo(pgok, 0);
                pgok->FChangeState(kstDefault);
            }

            pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidWorkspace);
            if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
            {
                AssertPo(pgok, 0);
                pgok->SetZPlane(0);
            }
        }
        else
        {

            if (!vpappb->FGetProp(kpridToolTipDelay, &_dtimToolTipDelay))
            {
                _dtimToolTipDelay = 3 * kdtimSecond;
            }

            vpappb->FSetProp(kpridToolTipDelay, 0x0FFFFFFF);
            if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
            {
                AssertPo(pgok, 0);
                pgok->FChangeState(kstFreeze);
            }
        }

        if (_pmvie->Pscen() != pvNil)
        {
            _pmvie->Play();
        }
    }

    return fTrue;
}

/***************************************************************************
 *
 * Handle New Scene command
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdNewScene(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    TAG tag;
    bool fEnable;

    vapp.BeginLongOp();

    tag = *((PTAG)(pcmd->rglw));

    if (pvNil != _pmvie)
    {
        AssertPo(_pmvie, 0);

        fEnable = (_pmvie->Pscen() == pvNil);

        if (_pmvie->FAddScen(&tag) && fEnable)
        {
            _SetToolStates();
        }
    }

    vapp.EndLongOp();

    return fTrue;
}

/***************************************************************************
 *
 * Handle changing the respecting of ground.
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdRespectGround(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovieView pmvu = (PMovieView)_pmvie->PddgGet(0);
    AssertPo(pmvu, 0);
    pmvu->SetFRespectGround(pcmd->rglw[0]);

    if ((pmvu->Tool() != toolCompose) && (pmvu->Tool() != toolRecordSameAction) && (pmvu->Tool() != toolAction) &&
        (pmvu->Tool() != toolPlace) && !pmvu->FTextMode())
    {
        pmvu->SetTool(toolCompose);
        ChangeTool(toolCompose);
    }

    return (fTrue);
}

/***************************************************************************
 *
 * Handle setting the pause.
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdPause(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (_pmvie->Pscen() == pvNil)
    {
        return (fTrue);
    }

    switch (pcmd->cid)
    {
    case cidPauseUntilClick:
        _pmvie->FPause(witUntilClick, 0);
        break;

    case cidPauseForSound:
        _pmvie->FPause(witUntilSnd, 0);
        break;

    case cidClearPause:
        _pmvie->FPause(witNil, 0);
        break;

    default:
        Bug("Unknown cid");
    }
    return (fTrue);
}

/***************************************************************************
    Open an existing or new chunky file for editing.
    Handles cidNew and cidOpen.
***************************************************************************/
bool Studio::FCmdOpen(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    switch (pcmd->cid)
    {
    default:
        Bug("why are we here?");
        return fFalse;

    case cidNew:
        if (_fDisplayCast && vpappb->GrfcustCur() & fcustShift)
        {
            Filename fni;
            STN stn;

            _fDisplayCast = fFalse;

            stn = PszLit("3dmovie.ms");
            if (vptagm->FFindFile(vapp.SidProduct(), &stn, &fni, fFalse))
            {
                if (FLoadMovie(&fni))
                    vpcex->EnqueueCid(cidPlay, this);
            }
        }
        else
            FLoadMovie();
        break;

    case cidOpen: {
        Filename fni;

        _fDisplayCast = fFalse;
        if (FGetFniMovieOpen(&fni))
        {
            // User selected a file, so open it.

            FLoadMovie(&fni, cnoNil);
        }

        break;
    }
    }

    return (fTrue);
}

/******************************************************************************
    FCmdScroll
        Handles scrollbar commands.

    Author: ******

    Returns: nothing

******************************************************************************/
bool Studio::FCmdScroll(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    AssertVarMem(_psmcc);
    if (pvNil == _psmcc->Psscb())
    {
        // absorb the cmd since there is no scrollbar
        return fTrue;
    }

    return _psmcc->Psscb()->FCmdScroll(pcmd);
}

/******************************************************************************
    FCmdSooner
        Handles the soonering an actor

    returns fTrue.

******************************************************************************/
bool Studio::FCmdSooner(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (!Pmvie()->FSoonerLaterActr(Pmvie()->Pscen()->Nfrm() - 1))
    {
        PMovieView pmvu;
        PKidspaceGraphicObject pgok;

        pmvu = (PMovieView)Pmvie()->PddgActive();
        pmvu->SetTool(toolCompose);

        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsSoonerButton);

        if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
        {
            AssertPo(pgok, 0);
            pgok->FChangeState(kstClosed);
        }
    }

    return fTrue;
}

/******************************************************************************
    FCmdSooner
        Handles the soonering an actor

    returns fTrue.

******************************************************************************/
bool Studio::FCmdLater(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    if (!Pmvie()->FSoonerLaterActr(Pmvie()->Pscen()->Nfrm() + 1))
    {
        PMovieView pmvu;
        PKidspaceGraphicObject pgok;

        pmvu = (PMovieView)Pmvie()->PddgActive();
        pmvu->SetTool(toolCompose);

        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsSoonerButton);

        if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
        {
            AssertPo(pgok, 0);
            pgok->FChangeState(kstClosed);
        }
    }

    return fTrue;
}

/******************************************************************************
    FCmdNewSpletter
        Handles creating new spletters.

    returns fTrue.

******************************************************************************/
bool Studio::FCmdNewSpletter(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    STN stn;
    TAG tagTdf;
    ChunkIdentification cki;
    PBCL pbcl;
    THD thd;

    vapp.BeginLongOp();
    vapp.GetStnProduct(&stn); // Default string for TDT easel is product name

    // Find the first font available for this source, cache it, and use it
    // as the default font for the TDT easel.
    cki.ctg = kctgTfth;
    cki.cno = cnoNil;
    pbcl = BCL::PbclNew(pvNil, &cki, ctgNil, pvNil, fTrue);
    if (pvNil == pbcl)
        return fTrue;
    if (pbcl->IthdMac() == 0)
    {
        ReleasePpo(&pbcl);
        return fTrue;
    }
    pbcl->GetThd(0, &thd);
    tagTdf = thd.tag;
    ReleasePpo(&pbcl);
    if (!vptagm->FCacheTagToHD(&tagTdf))
        return fTrue;

    // Note: easels are self-managing, so we don't need to keep the PESLT
    ESLT::PesltNew(_pcrm, _pmvie, pvNil, &stn, tdtsNormal, &tagTdf);

    vapp.EndLongOp();

    return fTrue;
}

/******************************************************************************
    Start the sound recording easel
******************************************************************************/
bool Studio::FCmdOpenSoundRecord(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    bool fSpeech = FPure(pcmd->rglw[0]); // Speech or SFX browser?

    // If default sound name is required then set it in stn here.
    // Currently no default sound name required, so initialize it empty.
    STN stn;

    vapp.BeginLongOp();

    // Note: easels are self-managing, so we don't need to keep the PESLR
    ESLR::PeslrNew(_pcrm, _pmvie, fSpeech, &stn);

    vapp.EndLongOp();

    return fTrue;
}

/******************************************************************************
    SceneChange
        Callback to handle a change of scene.

    Returns: nothing

************************************************************ PETED ***********/
void Studio::SceneChange(void)
{
}

/***************************************************************************
 *
 * Create a popup menu
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdCreatePopup(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    ChunkIdentification ckiRoot;
    long cid, kid, thumSel = ivNil;
    ulong grfchp;
    BWS bws = kbwsCnoRoot;
    CHP chp;
    PTBOX ptbox;

    ckiRoot.cno = cnoNil;

    Assert(_pmvie != pvNil, "No current Movie");
    Assert(_pmvie->Pscen() != pvNil, "No current Scene");
    if ((ptbox = _pmvie->Pscen()->PtboxSelected()) != pvNil)
        ptbox->FetchChpSel(&chp, &grfchp);
    else
    {
        grfchp = grfchpNil;
    }

    switch (pcmd->cid)
    {
    case cidTextColor:
        ckiRoot.ctg = kctgTcth;
        cid = cidTextSetColor;
        kid = kidTextColor;
        goto LCreate;

    case cidTextBkgdColor:
        ckiRoot.ctg = kctgTbth;
        cid = cidTextSetBkgdColor;
        kid = kidTextBkgdColor;
        goto LCreate;

    case cidTextSize:
        ckiRoot.ctg = kctgTzth;
        cid = cidTextSetSize;
        kid = kidTextSize;
        if (grfchp & kfchpDypFont)
            thumSel = chp.dypFont;
        goto LCreate;

    case cidTextStyle:
        ckiRoot.ctg = kctgTyth;
        cid = cidTextSetStyle;
        kid = kidTextStyle;
        if (grfchp & kfchpBold && grfchp & kfchpItalic)
            thumSel = chp.grfont;
        bws = kbwsIndex;

    LCreate:
        MP::PmpNew(kidBackground, kid, _pcrm, pcmd, bws, thumSel, ksidInvalid, ckiRoot, ctgNil, this, cid, fFalse);
        break;
    case cidTextFont: {
        PStringTable pgst;
        long onnCur, onnSystem = vntl.OnnSystem();

        if ((pgst = StringTable::PgstNew(size(onnCur))) == pvNil)
            break;

        for (onnCur = 0; onnCur < vntl.OnnMac(); onnCur++)
        {
            STN stn;

            if (onnCur != onnSystem)
            {
                vntl.GetStn(onnCur, &stn);
                if (!pgst->FAddStn(&stn, &onnCur))
                {
                    ReleasePpo(&pgst);
                    break;
                }
            }
        }
        if (pgst != pvNil)
        {
            MPFNT::PmpfntNew(_pcrm, kidBackground, kidTextFont, pcmd, (grfchp & kfchpOnn) ? chp.onn : ivNil, pgst);
            ReleasePpo(&pgst);
        }
        break;
    }
    default:
        BugVar("unexpected cid", &pcmd->cid);
        break;
    }

    return fTrue;
}

/***************************************************************************
 *
 * Sets the text box background color
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdTextSetBkgdColor(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    AssertIn(pcmd->rglw[0], 0, kbMax);

    byte iscr = (byte)pcmd->rglw[0];
    AbstractColor acr(iscr);
    PMovieView pmvu = (PMovieView)_pmvie->PddgActive();

    if (iscr == 0)
        acr.SetToClear();
    _pmvie->SetPaintAcr(acr);
    pmvu->SetTool(toolTboxFillBkgd);
    SetCurs(toolTboxFillBkgd);

    return fTrue;
}

/***************************************************************************
 *
 * Sets the text box text color
 *
 * Parameters:
 *  pcmd - Pointer to the command to process.
 *
 * Returns:
 *  fTrue if it handled the command, else fFalse.
 *
 **************************************************************************/
bool Studio::FCmdTextSetColor(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    AssertIn(pcmd->rglw[0], 0, kbMax);

    byte iscr = (byte)pcmd->rglw[0];
    AbstractColor acr(iscr);
    PMovieView pmvu = (PMovieView)_pmvie->PddgActive();

    if (iscr == 0)
        acr.SetToClear();
    _pmvie->SetPaintAcr(acr);
    pmvu->SetTool(toolTboxPaintText);
    SetCurs(toolTboxPaintText);

    return fTrue;
}

/******************************************************************************
    FCmdTextSetSize
        Command handler to set the font size for the active textbox

    Arguments:
        PCMD pcmd  --  rglw[0] holds the new size

    Returns:  fTrue; always handles the command

************************************************************ PETED ***********/
bool Studio::FCmdTextSetSize(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovieView pmvu = _pmvie->PmvuCur();

    _pmvie->SetDypFontTextCur(pcmd->rglw[0]);
    pmvu->SetTool(toolTboxSize);
    SetCurs(toolTboxSize);

    return fTrue;
}

/******************************************************************************
    FCmdTextSetStyle
        Command handler to set the font Style for the active textbox

    Arguments:
        PCMD pcmd  --
            rglw[0] holds the mask for the new style bits
            rglw[1] holds the new style bits

    Returns:  fTrue; always handles the command

************************************************************ PETED ***********/
bool Studio::FCmdTextSetStyle(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    ulong grfont;
    PMovieView pmvu = _pmvie->PmvuCur();

    grfont = pmvu->GrfontStyleTextCur();
    grfont &= ~((ulong)pcmd->rglw[0]);
    grfont |= (ulong)pcmd->rglw[1];

    _pmvie->SetStyleTextCur(grfont);
    pmvu->SetTool(toolTboxStyle);
    SetCurs(toolTboxStyle);

    return fTrue;
}

/******************************************************************************
    FCmdTextSetFont
        Command handler to set the font face for the active textbox

    Arguments:
        PCMD pcmd  --  rglw[0] holds the new face

    Returns:  fTrue; always handles the command

************************************************************ PETED ***********/
bool Studio::FCmdTextSetFont(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PMovieView pmvu = _pmvie->PmvuCur();

    _pmvie->SetOnnTextCur(pcmd->rglw[0]);
    pmvu->SetTool(toolTboxFont);
    SetCurs(toolTboxFont);

    return fTrue;
}

/***************************************************************************
    Load a cursor only.  Do not set the tool permanently.  Used for
    roll over cursors.
***************************************************************************/
void Studio::SetCurs(long tool)
{
    PCURS pcurs;
    long cursID;

    switch (tool)
    {
    case toolListener:
        cursID = kcrsListener;
        break;
    case toolSounder:
        cursID = kcrsSounder;
        break;
    case toolLooper:
        cursID = kcrsLooper;
        break;
    case toolMatcher:
        cursID = kcrsMatcher;
        break;
    case toolActorSelect:
        cursID = kcrsHand;
        break;
    case toolCompose:
        cursID = kcrsCompose;
        break;
    case toolComposeAll:
        cursID = kcrsComposeAll;
        break;
    case toolSquashStretch:
        cursID = kcrsSquashStretch;
        break;
    case toolSoonerLater:
        cursID = kcrsSoonerLater;
        break;
    case toolResize:
        cursID = kcrsResize;
        break;
    case toolRotateX:
        cursID = kcrsRotateX;
        break;
    case toolRotateY:
        cursID = kcrsRotateY;
        break;
    case toolRotateZ:
        cursID = kcrsRotateZ;
        break;
    case toolCostumeCmid:
    case toolActorEasel:
        cursID = kcrsCostume;
        break;
    case toolAction:
        cursID = kcrsActionBrowser;
        break;
    case toolRecordSameAction:
        cursID = kcrsRecord;
        break;
    case toolTweak:
        cursID = kcrsTweak;
        break;
    case toolNormalizeRot:
        cursID = kcrsNormalizeRot;
        break;
    case toolNormalizeSize:
        cursID = kcrsNormalizeSize;
        break;
    case toolDefault:
        cursID = kcrsDefault;
        break;
    case toolTboxMove:
        cursID = kcrsTboxMove;
        break;
    case toolTboxUpDown:
        cursID = kcrsTboxUpDown;
        break;
    case toolTboxLeftRight:
        cursID = kcrsTboxLeftRight;
        break;
    case toolTboxFalling:
        cursID = kcrsTboxFalling;
        break;
    case toolTboxRising:
        cursID = kcrsTboxRising;
        break;
    case toolSceneNuke:
    case toolActorNuke:
        cursID = kcrsNuke;
        break;
    case toolIBeam:
        cursID = kcrsIBeam;
        break;
    case toolCutObject:
        cursID = kcrsCutObject;
        break;
    case toolCopyObject:
        cursID = kcrsCopyObject;
        break;
    case toolCopyRte:
        cursID = kcrsCopyRte;
        break;
    case toolSceneChop:
        cursID = kcrsSceneChop;
        break;
    case toolSceneChopBack:
        cursID = kcrsSceneChopBack;
        break;
    case toolCutText:
        cursID = kcrsCutText;
        break;
    case toolCopyText:
        cursID = kcrsCopyText;
        break;
    case toolPasteText:
        cursID = kcrsPasteText;
        break;
    case toolPasteRte:
        cursID = kcrsPasteRte;
        break;
    case toolPasteObject:
        cursID = kcrsDefault;
        break;
    case toolTboxPaintText:
        cursID = kcrsPaintText;
        break;
    case toolTboxFillBkgd:
        cursID = kcrsFillBkgd;
        break;
    case toolTboxStory:
        cursID = kcrsTboxStory;
        break;
    case toolTboxCredit:
        cursID = kcrsTboxCredit;
        break;
    case toolTboxFont:
        cursID = kcrsTboxFont;
        break;
    case toolTboxSize:
        cursID = kcrsTboxFontSize;
        break;
    case toolTboxStyle:
        cursID = kcrsTboxFontStyle;
        break;

    default:
        Bug("Unknown tool type");
    }

    pcurs = (PCURS)_pcrm->PbacoFetch('GGCR', cursID, CURS::FReadCurs);
    if (pvNil != pcurs)
    {
        vpappb->SetCurs(pcurs);
        ReleasePpo(&pcurs);
    }
}

/***************************************************************************
    Tell the play button to go back to Play
***************************************************************************/
void Studio::PlayStopped(void)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidPlay);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject) && (pgok->Sno() != kstDefault))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstDefault);
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsActionBrowser);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstDefault);
    }

    vpappb->FSetProp(kpridToolTipDelay, _dtimToolTipDelay);
}

/***************************************************************************
    The movie engine changed the tool, now change the UI
***************************************************************************/
void Studio::ChangeTool(long tool)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    if (tool == toolTboxMove)
    {
        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidTextsCover);
    }
    else if (tool == toolDefault)
    {
        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidSettingsCover);
    }
    else
    {
        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsCover);
    }

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject) && (pgok->Sno() != kstOpen))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstOpen);
    }

    pgok = pvNil;

    switch (tool)
    {
    case toolCompose:
        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsCompose);
        break;

    case toolRecordSameAction:
        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsContinue);
        break;

    case toolTboxMove:
        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidTextsSelect);
        break;

    case toolDefault:
        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidSettingsBackground);
        pgok->FRunScript((kstDefault << 16) | kchidResetTools);
        return;

    default:
        break;
    }

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject) && (pgok->Sno() != kstSelected))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstSelected);
    }
}

/***************************************************************************
    The movie engine deleted the scene.
***************************************************************************/
void Studio::SceneNuked(void)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    if (_pmvie->Pscen() == pvNil)
    {

        _SetToolStates();
        _psmcc->UpdateRollCall();

        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidUndosCover);

        if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
        {
            AssertPo(pgok, 0);
            pgok->FChangeState(kstOpen);
        }
    }
}

/***************************************************************************
    The movie engine Undeleted a scene.
***************************************************************************/
void Studio::SceneUnnuked(void)
{
    AssertThis(0);

    if (_pmvie->Cscen() != 1)
    {
        return;
    }

    _SetToolStates();
    _psmcc->UpdateRollCall();

    if (_pmvie->Pscen()->Cactr() > 0)
    {
        EnableActorTools();
    }

    if (_pmvie->Pscen()->Ctbox() > 0)
    {
        EnableTboxTools();
    }
}

/***************************************************************************
    The movie engine deleted an actor.
***************************************************************************/
void Studio::ActorNuked(void)
{
    AssertThis(0);

    if (_pmvie->Pscen() == pvNil)
    {
        _SetToolStates();
    }
}

/***************************************************************************
    Enables/Disables the tools
***************************************************************************/
void Studio::_SetToolStates(void)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject) && (_pmvie->Pscen() == pvNil))
    {
        AssertPo(pgok, 0);
        pgok->FRunScript((kstDefault << 16) | kchidResetTools);
        return;
    }

    //
    // Enable everything, since we now have a scene.
    //
    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FRunScript((kstDefault << 16) | kchidEnableSceneTools);
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsCover);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstClosed);
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidSoundsCover);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstClosed);
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidTextsCover);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstClosed);
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidScrollbarsCover);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstOpen);
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidUndosCover);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstOpen);
    }

    SetUndo(_pmvie->CundbUndo() != 0 ? undoUndo : _pmvie->CundbRedo() != 0 ? undoRedo : undoDisabled);

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBooksCover);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstOpen);
    }
}

/***************************************************************************
    The movie engine inserted the first actor into the movie.
***************************************************************************/
void Studio::EnableActorTools(void)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FRunScript((kstDefault << 16) | kchidEnableActorTools);
    }
}

/***************************************************************************
    The movie engine inserted the first actor into the movie.
***************************************************************************/
void Studio::EnableTboxTools(void)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FRunScript((kstDefault << 16) | kchidEnableTboxTools);
    }
}

/***************************************************************************
    The movie engine has a new selected textbox.
***************************************************************************/
void Studio::TboxSelected(void)
{
    AssertThis(0);
}

/***************************************************************************
    The movie engine has a new undo buffer state
***************************************************************************/
void Studio::SetUndo(long undo)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidUndo);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(undo == undoUndo ? kstOpen : (undo == undoRedo ? kstClosed : kstDisabled));
    }
}

/***************************************************************************
    Put up the costume changer / 3-D Text easel.  Returns fTrue if user
    made changes, else fFalse.
***************************************************************************/
void Studio::ActorEasel(bool *pfActrChanged)
{
    AssertThis(0);
    AssertVarMem(pfActrChanged);

    vpcex->EnqueueCid(cidActorEaselOpen);
    *pfActrChanged = fFalse;
}

/***************************************************************************
    Put up the costume changer / 3-D Text easel.  Returns fTrue if user
    made changes, else fFalse.
***************************************************************************/
bool Studio::FCmdActorEaselOpen(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PActor pactr = _pmvie->Pscen()->PactrSelected();
    AssertPo(pactr, 0);

    vapp.BeginLongOp();

    //
    // Start the easel.
    // Note: easels are self-managing, so we don't need to keep the PESL
    //
    if (pactr->Ptmpl()->FIsTdt())
    {
        ESLT::PesltNew(_pcrm, _pmvie, pactr);
    }
    else
    {
        ESLA::PeslaNew(_pcrm, _pmvie, pactr);
    }

    vapp.EndLongOp();
    return fTrue;
}

/***************************************************************************
    This frame has a pause type.
***************************************************************************/
void Studio::PauseType(WIT wit)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;
    long fFlag;

    fFlag = FPure(wit == witUntilSnd);

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidPausesSound);
    AssertNilOrPo(pgok, 0);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {

        if (fFlag)
        {
            pgok->FChangeState(kstOpen);
        }
        else
        {
            pgok->FChangeState(kstClosed);
        }
    }
    else
    {

        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);
        if (pgok != pvNil)
        {
            pgok->FRunScript((kstDefault << 16) | kchidSetPauseType, &fFlag, 1);
        }
    }

    if (!_pmvie->FPlaying())
    {
        return;
    }
}

/***************************************************************************
    The movie engine is recording
***************************************************************************/
void Studio::Recording(bool fRecording, bool fRecord)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidPlay);
    AssertNilOrPo(pgok, 0);

    if (pgok == pvNil)
    {
        return;
    }

    if (fRecording)
    {

        if (pgok->Sno() != kstRecording)
        {
            pgok->FChangeState(kstRecording);
        }
    }
    else
    {

        if (pgok->Sno() != kstDefault)
        {
            pgok->FChangeState(kstDefault);
        }
    }
}

/***************************************************************************
    The movie engine has an actor ready to be sooner/latered
***************************************************************************/
void Studio::StartSoonerLater(void)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsSoonerButton);
    AssertNilOrPo(pgok, 0);

    if (pgok == pvNil)
    {
        return;
    }

    pgok->FChangeState(kstOpen);

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsLaterButton);
    AssertNilOrPo(pgok, 0);

    if (pgok == pvNil)
    {
        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsSoonerButton);
        AssertPo(pgok, 0);
        pgok->FChangeState(kstClosed);
        return;
    }

    pgok->FChangeState(kstOpen);
    vpapp->DisableAccel();
    _psmcc->Psscb()->StartNoAutoadjust();

    _fStartedSoonerLater = fTrue;
}

/***************************************************************************
    The movie engine has completed a sooner/later
***************************************************************************/
void Studio::EndSoonerLater(void)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;

    if (!_fStartedSoonerLater)
        return;

    _psmcc->Psscb()->EndNoAutoadjust();

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsSoonerButton);
    AssertNilOrPo(pgok, 0);

    if (pgok != pvNil)
    {
        pgok->FChangeState(kstClosed);
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsLaterButton);
    AssertNilOrPo(pgok, 0);

    if (pgok != pvNil)
    {
        pgok->FChangeState(kstClosed);
    }

    vpapp->EnableAccel();
    _fStartedSoonerLater = fFalse;
}

/***************************************************************************
    The movie engine has placed an actor.
***************************************************************************/
void Studio::NewActor(void)
{
    AssertThis(0);

    PKidspaceGraphicObject pgok;
    PMovieView pmvu;

    pmvu = (PMovieView)_pmvie->PddgGet(0);
    AssertPo(pmvu, 0);

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsXY);
    AssertNilOrPo(pgok, 0);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        if (pgok->Sno() == kstOpen)
        {
            pgok->FChangeState(kstClosed);
        }
    }
    else
    {
        BRS rgr[3][3] = {{rOne, rZero, rZero}, {rZero, rZero, rOne}, {rZero, -rOne, rZero}};

        pmvu->SetAxis(rgr);

        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);
        if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
        {
            AssertPo(pgok, 0);
            pgok->FRunScript((kstDefault << 16) | kchidResetXZAxisAndGround);
        }
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsGround);
    AssertNilOrPo(pgok, 0);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        if (pgok->Sno() == kstClosed)
        {
            pgok->FChangeState(kstOpen);
        }
    }
    else
    {
        pmvu->SetFRespectGround(fFalse);

        pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);
        if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
        {
            AssertPo(pgok, 0);
            pgok->FRunScript((kstDefault << 16) | kchidResetXZAxisAndGround);
        }
    }

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidActorsCompose);
    AssertNilOrPo(pgok, 0);

    if ((pgok != pvNil) && pgok->FIs(kclsKidspaceGraphicObject))
    {
        pgok->FChangeState(kstSelected);
    }
}

/***************************************************************************
    The RollCall needs a mapping from Tmpl ChunkNumber's to the KidspaceGraphicObjectDescriptor thumb cno
***************************************************************************/
bool Studio::FAddCmg(ChunkNumber cnoTmpl, ChunkNumber cnoGokd)
{
    AssertThis(0);

    CMG cmg;

#ifdef DEBUG
    long icmg;
    for (icmg = 0; icmg < _pglcmg->IvMac(); icmg++)
    {
        _pglcmg->Get(icmg, &cmg);
        if (cmg.cnoTmpl == cnoTmpl)
        {
            Warn("Duplicate actor cno in content");
            return fTrue;
        }
    }
#endif // DEBUG

    cmg.cnoTmpl = cnoTmpl;
    cmg.cnoGokd = cnoGokd;
    return _pglcmg->FAdd(&cmg);
}

/***************************************************************************
    Return the cnoGokd corres to the cnoTmpl
***************************************************************************/
ChunkNumber Studio::CnoGokdFromCnoTmpl(ChunkNumber cnoTmpl)
{
    AssertThis(0);

    long icmg;
    CMG cmg;

    for (icmg = 0; icmg < _pglcmg->IvMac(); icmg++)
    {
        _pglcmg->Get(icmg, &cmg);
        if (cmg.cnoTmpl == cnoTmpl)
            return cmg.cnoGokd;
    }

    return cnoNil;
}

/***************************************************************************
    The movie engine needs the action browser for the currently selected actor
***************************************************************************/
void Studio::StartActionBrowser(void)
{
    AssertThis(0);
    AssertPo(_pmvie->Pscen(), 0);
    AssertPo(_pmvie->Pscen()->PactrSelected(), 0);

    PKidspaceGraphicObject pgok;
    CMD cmd;

    pgok = (PKidspaceGraphicObject)((APP *)vpappb)->Pkwa()->PgobFromHid(kidBackground);

    if (pgok != pvNil)
    {
        cmd = _cmd;
        cmd.cid = cidBrowserReady;
        cmd.pcmh = this;
        cmd.rglw[0] = kidBrwsAction;
        vpcex->EnqueueCmd(&cmd);
    }
}

/***************************************************************************
    Start the listener easel
***************************************************************************/
void Studio::StartListenerEasel(void)
{
    AssertThis(0);

    vpcex->EnqueueCid(cidListenerEaselOpen);
}

/***************************************************************************
    Start the listener easel
***************************************************************************/
bool Studio::FCmdListenerEaselOpen(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    // Note: easels are self-managing, so we don't need to keep the PESLL
    ESLL::PesllNew(_pcrm, _pmvie, _pmvie->Pscen()->PactrSelected());
    return fTrue;
}

/***************************************************************************
    Try to exit the studio.  The fClearCache flag is fTrue by default,
    but should bet set to fFalse if the app is about to quit so we don't
    bother clearing the caches (since it's slow and requires the CD(s) to
    be in.)
***************************************************************************/
bool Studio::FShutdown(bool fClearCache)
{
    AssertThis(0);

    bool fRet = fTrue;
    PMovieView pmvu;

    if (_pmvie != pvNil)
    {
        pmvu = (PMovieView)_pmvie->PddgActive();
        AssertPo(pmvu, 0);

        fRet = pmvu->FCloseDoc(fFalse);
    }

    if (fRet && fClearCache)
        vptagm->ClearCache();

    return fRet;
}

/***************************************************************************
    Static function to stop the action button animation
***************************************************************************/
void Studio::PauseActionButton(void)
{
    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)vpapp->Pkwa()->PgobFromHid(kidActorsActionBrowser);
    if (pgok != pvNil && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstFreeze);
    }
}

/***************************************************************************
    Static function to resume the action button animation
***************************************************************************/
void Studio::ResumeActionButton(void)
{
    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)vpapp->Pkwa()->PgobFromHid(kidActorsActionBrowser);
    if (pgok != pvNil && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        pgok->FChangeState(kstDefault);
    }
}

/***************************************************************************
    The movie engine is using the tool, now play the sound
***************************************************************************/
void Studio::PlayUISound(long tool, long grfcust)
{
    AssertThis(0);

    PChunkyResourceFile pcrf;
    long cactRepeat = 1;
    long cno;

    return; // hack to disable UI sounds

    _fStopUISound = fFalse;

    switch (tool)
    {

    case toolUndo:
        cno = kcnoUndoWav;
        break;

    case toolRedo:
        cno = kcnoRedoWav;
        break;

    case toolTboxMove:
        return;

    case toolCompose:
        if (grfcust & fcustCmd)
        {
            cno = kcnoMove1Wav;
        }
        else if (grfcust & fcustShift)
        {
            cno = kcnoMoveAllWav;
        }
        else
        {
            cno = kcnoMoveWav;
        }
        break;

    case toolActorSelect:
        cno = kcnoSelectWav;
        break;

    case toolSounder:
        cno = kcnoSPlayWav;
        break;

    case toolLooper:
        cno = kcnoSLoopWav;
        break;

    case toolMatcher:
        cno = kcnoSActWav;
        break;

    case toolSceneNuke:
        cno = kcnoRemScnWav;
        break;

    case toolActorNuke:
        if (grfcust & fcustShift)
        {
            cno = kcnoRemWBoxWav;
        }
        else
        {
            cno = kcnoRemActrWav;
        }
        break;

    case toolSceneChop:
        cno = kcnoRemAftrWav;
        break;

    case toolSceneChopBack:
        cno = kcnoRemBfrWav;
        break;

    case toolTboxStory:
        cno = kcnoWScrOffWav;
        break;

    case toolTboxCredit:
        cno = kcnoWScrOnWav;
        break;

    case toolTboxPaintText:
        cno = kcnoWColorWav;
        break;

    case toolTboxFillBkgd:
        cno = kcnoWBgClrWav;
        break;

    case toolTboxFont:
        cno = kcnoWFontWav;
        break;

    case toolTboxSize:
        cno = kcnoWSizeWav;
        break;

    case toolTboxStyle:
        cno = kcnoWStyleWav;
        break;

    case toolSoonerLater:
        cno = kcnoSFreezeWav;
        break;

    case toolRotateX:
    case toolRotateY:
    case toolRotateZ:
        cno = kcnoRotateWav;
        cactRepeat = -1;
        _fStopUISound = fTrue;
        break;

    case toolResize:
        _fStopUISound = fTrue;
        if (grfcust & fcustShift)
        {
            cno = kcnoShrinkWav;
        }
        else
        {
            cno = kcnoGrowWav;
        }
        break;

    case toolSquashStretch:
        _fStopUISound = fTrue;
        if (grfcust & fcustShift)
        {
            cno = kcnoSquashWav;
        }
        else
        {
            cno = kcnoStretchWav;
        }
        break;

    case toolNormalizeRot:
        cno = kcnoCBackRWav;
        break;

    case toolNormalizeSize:
        cno = kcnoCBackSWav;
        break;

    case toolRecordSameAction:
        cno = kcnoRecordWav;
        break;

    case toolAddAFrame:
        cno = kcnoAddFrameWav;
        break;

    case toolFWAFrame:
        cno = kcnoGoNextFWav;
        break;

    case toolRWAFrame:
        cno = kcnoGoPrevFWav;
        break;

    case toolFWAScene:
        cno = kcnoGoNextSWav;
        break;

    case toolRWAScene:
        cno = kcnoGoPrevSWav;
        break;

    case toolPasteObject:
    case toolPasteText:
        cno = kcnoPasteWav;
        break;

    case toolCutObject:
    case toolCutText:
        cno = kcnoCutWav;
        break;

    case toolCopyObject:
    case toolCopyText:
        cno = kcnoCopyWav;
        break;

    case toolCopyRte:
        cno = kcnoCopyPWav;
        break;

    case toolDefault:
        return;

    default:
        Bug("No Sound for this tool");
        return;
    }

    //
    // Find the sound and play it
    //
    if ((pcrf = _pcrm->PcrfFindChunk(kctgWave, cno)) != pvNil)
    {
        vpsndm->SiiPlay(pcrf, kctgWave, cno, ksqnNone, kvlmFull, cactRepeat, 0, 0, ksclUISound);
    }
}

/***************************************************************************
    The movie engine is done using the tool, now stop the sound
***************************************************************************/
void Studio::StopUISound(void)
{
    AssertThis(0);

    if (_fStopUISound)
    {
        vpsndm->StopAll(sqnNil, ksclUISound);
    }
}

/***************************************************************************
    Read a Misc Studio stn
***************************************************************************/
void Studio::GetStnMisc(long ids, PSTN pstn)
{
    AssertBaseThis(0);
    AssertDo(_pgstMisc->FFindExtra(&ids, pstn), "Invalid studio.cht or ids");
}

/******************************************************************************
    FCmdToggleXY
        Command handler to toggle the XY button setting in the studio

    Arguments:
        PCMD pcmd  --  the command to process

    Returns:  fTrue; always handles the command

************************************************************ SEANSE ***********/
bool Studio::FCmdToggleXY(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)vpapp->Pkwa()->PgobFromHid(kidActorsXY);
    if (pgok != pvNil && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        vpcex->EnqueueCid(cidClicked, pgok, pvNil, pvNil);
    }

    return fTrue;
}

/******************************************************************************
    FCmdHelpBook
        Command handler to bring up the help book

    Arguments:
        PCMD pcmd  --  the command to process

    Returns:  fTrue; always handles the command

************************************************************ SEANSE ***********/
bool Studio::FCmdHelpBook(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    PKidspaceGraphicObject pgok;

    pgok = (PKidspaceGraphicObject)vpapp->Pkwa()->PgobFromHid(kidBook);

    if (pgok != pvNil && pgok->FIs(kclsKidspaceGraphicObject))
    {
        AssertPo(pgok, 0);
        vpcex->EnqueueCid(cidClicked, pgok, pvNil, pvNil);
    }

    return fTrue;
}

/******************************************************************************
    FCmdMovieGoto
        Command handler to force the movie to a specific scene and frame number

    Arguments:
        PCMD pcmd  --  the command to process

    Returns:  fTrue; always handles the command

************************************************************ SEANSE ***********/
bool Studio::FCmdMovieGoto(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    /* REVIEW seanse (peted): not good!  This allows script to pump whatever
        number it feels like into our code, with no range checking. */
    _pmvie->LwSetMoviePos(pcmd->rglw[0], pcmd->rglw[1]);
    return fTrue;
}

/******************************************************************************
    FCmdSoundsEnabled
        Command handler to enabled/disable movie sounds.

    Arguments:
        PCMD pcmd  --  the command to process

    Returns:  fTrue; always handles the command

************************************************************ SEANSE ***********/
bool Studio::FCmdSoundsEnabled(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);
    _pmvie->SetFSoundsEnabled(FPure(pcmd->rglw[0]));
    return fTrue;
}

/******************************************************************************
    FCmdCreateTbox
        Command handler to create a textbox.

    Arguments:
        PCMD pcmd  --  the command to process

    Returns:  fTrue; always handles the command

************************************************************ SEANSE ***********/
bool Studio::FCmdCreateTbox(PCMD pcmd)
{
    AssertThis(0);
    AssertVarMem(pcmd);

    RC rc;

    rc.Set(kxpDefaultTbox, kypDefaultTbox, kxpDefaultTbox + kdxpDefaultTbox, kypDefaultTbox + kdypDefaultTbox);

    if (_pmvie->FInsTbox(&rc, !FPure(pcmd->rglw[0])))
    {
        EnableTboxTools();
    }

    return fTrue;
}

/****************************************************
 * Updates the title display
 *
 * Parameters:
 *  pstnTitle - The new title to display.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Studio::UpdateTitle(PSTN pstnTitle)
{
    AssertThis(0);
    AssertPo(pstnTitle, 0);

    STN stnFontSize;
    long dypFontSize;

    // Set the movie title
    if (_ptgobTitle == pvNil)
    {
        _ptgobTitle = TGOB::PtgobCreate(kidName, idsStudioFont, tavCenter);

        GetStnMisc(idsMovieNameDypFont, &stnFontSize);

        if (stnFontSize.FGetLw(&dypFontSize) && dypFontSize > 0)
        {
            _ptgobTitle->SetFontSize(dypFontSize);
        }
        else
        {
            Warn("Update Movie Name font size failed");
        }
    }

    if (_ptgobTitle != pvNil)
    {
        _ptgobTitle->SetText(pstnTitle);
    }
}

#ifdef DEBUG
/******************************************************************************
        Tells the movie to write bitmaps as it plays, and plays the movie.
************************************************************ PETED ***********/
bool Studio::FCmdWriteBmps(PCMD pcmd)
{
    if (_pmvie != pvNil)
    {
        AssertPo(_pmvie, 0);
        _pmvie->SetFWriteBmps(fTrue);
        vpcex->EnqueueCid(cidPlay, this);
    }
    return fTrue;
}
#endif // DEBUG

#ifdef DEBUG

/****************************************************
 * Mark memory used by the Studio
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 *
 ****************************************************/
void Studio::MarkMem(void)
{
    AssertThis(0);
    Studio_PAR::MarkMem();
    MarkMemObj(_pmvie);
    MarkMemObj(_psmcc);
    MarkMemObj(_pcrm);
    MarkMemObj(_pgstMisc);
    MarkMemObj(_pglpbrcn);
    MarkMemObj(_pbrwrActr);
    MarkMemObj(_pbrwrProp);
    MarkMemObj(_pglcmg);
    MarkMemObj(_pglclr);
    MarkMemObj(_ptgobTitle);
    TDT::MarkActionNames();

    //
    // Mark browser objects
    //
    long ipbrcn;
    PBRCN pbrcn;

    if (_pglpbrcn != pvNil)
    {
        for (ipbrcn = 0; ipbrcn < _pglpbrcn->IvMac(); ipbrcn++)
        {
            _pglpbrcn->Get(ipbrcn, &pbrcn);
            MarkMemObj(pbrcn);
        }
    }
}

/***************************************************************************
 *
 * Assert the validity of the Studio.
 *
 * Parameters:
 *  grf - Bit field of options
 *
 * Returns:
 *  None.
 *
 **************************************************************************/
void Studio::AssertValid(ulong grf)
{
    Studio_PAR::AssertValid(fobjAllocated);
    AssertNilOrPo(_pmvie, 0);
    AssertPo(_pcrm, 0);
    AssertPo(_pgstMisc, 0);
    AssertNilOrPo(_pbrwrActr, 0);
    AssertNilOrPo(_pbrwrProp, 0);
    AssertNilOrPo(_pglcmg, 0);
    AssertNilOrPo(_pglclr, 0);
    AssertNilOrPo(_pglpbrcn, 0);
    AssertNilOrPo(_ptgobTitle, 0);
}

#endif // DEBUG

/***************************************************************************
    Constructor for StudioClientCallbacks.
***************************************************************************/
StudioClientCallbacks::StudioClientCallbacks(long dxp, long dyp, long cbCache, PStudioScrollbars psscb, PStudio pstdio) : MovieClientCallbacks(dxp, dyp, cbCache)
{
    AssertNilOrPo(psscb, 0);
    // Note: Would like to do an AssertPo here but can't
    // the studio isn't necessarily set up yet.
    AssertBasePo(pstdio, 0);

    _psscb = psscb;
    if (pvNil != _psscb)
        _psscb->AddRef();
    _pstdio = pstdio;
    _dypTextTbox = 0;
}

/***************************************************************************
 *
 * Update RollCall
 *
 **************************************************************************/
void StudioClientCallbacks::UpdateRollCall(void)
{
    AssertThis(0);

    bool fUpdateActr, fUpdateProp;
    long aridSel;

    aridSel = _pstdio->AridSelected();

    if (_pstdio->PbrwrActr() != pvNil)
    {
        fUpdateActr = !_pstdio->PbrwrActr()->FApplyingSel();
        if (!fUpdateActr)
            aridSel = aridNil;
    }
    else
        fUpdateActr = fFalse;
    if (_pstdio->PbrwrProp() != pvNil)
    {
        fUpdateProp = !_pstdio->PbrwrProp()->FApplyingSel();
        if (!fUpdateProp)
            aridSel = aridNil;
    }
    else
        fUpdateProp = fFalse;

    if (fUpdateActr)
        _pstdio->PbrwrActr()->FUpdate(aridSel, _pstdio);

    if (fUpdateProp)
        _pstdio->PbrwrProp()->FUpdate(aridSel, _pstdio);
}

/******************************************************************************
    DypTextDef
        Retrieve a default text size for a textbox in a movie.
************************************************************ PETED ***********/
long StudioClientCallbacks::DypTboxDef(void)
{
    if (_dypTextTbox == 0)
    {
        STN stn;

        _pstdio->GetStnMisc(idsTboxDypFont, &stn);
        if (!stn.FGetLw(&_dypTextTbox) || _dypTextTbox <= 0)
            _dypTextTbox = vapp.DypTextDef();
    }
    return _dypTextTbox;
}

/******************************************************************************
    FQueryPurgeSounds
        Displays a query to the user, asking whether to purge unused imported
        sounds in the file.

    Returns: fTrue if the user wants the sounds purged

************************************************************ PETED ***********/
bool StudioClientCallbacks::FQueryPurgeSounds(void)
{
    AssertThis(0);

    STN stnMsg;

    AssertDo(vapp.FGetStnApp(idsPurgeSounds, &stnMsg), "String not present");
    return vapp.TModal(vapp.PcrmAll(), ktpcQueryPurgeSounds, &stnMsg, bkYesNo) == tYes;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of the StudioClientCallbacks
***************************************************************************/
void StudioClientCallbacks::AssertValid(ulong grf)
{
    StudioClientCallbacks_PAR::AssertValid(0);
    AssertPo(_psscb, 0);
}

/***************************************************************************
    Mark memory used by the StudioClientCallbacks
***************************************************************************/
void StudioClientCallbacks::MarkMem(void)
{
    AssertThis(0);
    StudioClientCallbacks_PAR::MarkMem();
    MarkMemObj(_psscb);
}

#endif // DEBUG
