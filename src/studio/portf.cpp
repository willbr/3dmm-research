/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************

    portf.cpp: Portfolio handler

    Primary Author: ******
    Review Status: peted has reviewed. Final version not yet approved.

***************************************************************************/
#include "studio.h"
#include "portfolio_host.h"

ASSERTNAME

/***************************************************************************

 FPortDisplayWithIds: Display the portfolio to open or save a file. Portfolio
                    title and filters generated using supplied string ids.

 Arguments: fni 			- Output Filename for file selected
            fOpen			- fTrue if open portfolio, else save portfolio.
            lFilterLabel	- id of portfolio filter label
            lFilterExt		- id of portfolio filter extension
            lTitle			- id of portfolio title
            lpstrDefExt		- Ptr to string for default file extension for save if required.
            pstnDefFileName - Ptr to default extension stn if required.
            pfniInitialDir	- Ptr to initial directory fni if required.
            grfPrevType		- Bits for types of preview required, (eg movie, sound etc) == 0 if no preview
            cnoWave         - Wave cno for audio when portfolio is invoked

 Returns: 	TRUE	- File selected
            FALSE	- User canceled portfolio.

***************************************************************************/
bool FPortDisplayWithIds(Filename *pfni, bool fOpen, LegacyLong lFilterLabel, LegacyLong lFilterExt, LegacyLong lTitle, achar *lpstrDefExt,
                         PString pstnDefFileName, Filename *pfniInitialDir, LegacyUlong grfPrevType, ChunkNumber cnoWave)
{
    String stnTitle;
    String stnFilterLabel;
    String stnFilterExt;
    int cChLabel, cChExt;
    ZString szFilter;
    bool fRet;

    AssertVarMem(pfni);
    AssertNilOrPo(pfniInitialDir, 0);
    AssertNilOrPo(pstnDefFileName, 0);

    if (!vapp.FGetStnApp(lTitle, &stnTitle))
        return fFalse;

    // Filter string contain non-terminating null characters, so must
    // build up string from separate label and file extension strings.

    if (!vapp.FGetStnApp(lFilterLabel, &stnFilterLabel))
        return fFalse;
    if (!vapp.FGetStnApp(lFilterExt, &stnFilterExt))
        return fFalse;
#ifdef KAUAI_SDL3
    if (lFilterExt == idsPortfMovieFilterExt)
    {
        stnFilterLabel.SetSz(PszLit("3DMM movies (*.3mm, *.vmm)"));
        stnFilterExt.SetSz(PszLit("*.3mm;*.vmm"));
    }
#endif

    // Kauai does not like internal null chars in an String. So build
    // up the final final string as an ZString.

    cChLabel = stnFilterLabel.Cch();
    cChExt = stnFilterExt.Cch();

    stnFilterLabel.GetSz(szFilter);
    stnFilterExt.GetSz(&szFilter[cChLabel + 1]);

    szFilter[cChLabel + cChExt + 2] = chNil;

    // Now display the open or save portfolio as required
    StopAllMovieSounds();
    vapp.EnsureInteractive();
    vapp.SetFInPortfolio(fTrue);
    if (fOpen)
    {
        fRet = FPortGetFniOpen(pfni, szFilter, stnTitle.Prgch(), pfniInitialDir, grfPrevType, cnoWave);
    }
    else
    {
        fRet = FPortGetFniSave(pfni, szFilter, stnTitle.Prgch(), lpstrDefExt, pstnDefFileName, grfPrevType, cnoWave);
    }

    // Make sure no portfolio related audio is still playing.
    vpsndm->StopAll(sqnNil, sclNil);
    vapp.SetFInPortfolio(fFalse);

    // Let the script know what the outcome is.
    vpcex->EnqueueCid(cidPortfolioClosed, 0, 0, fRet);

    // We must also enqueue a message for the help system here.
    // Help doesn't get the above cidPortfolioClosed as it has
    // already been filtered.
    vpcex->EnqueueCid(cidPortfolioResult, 0, 0, fRet);

    return fRet;
}

bool FPortGetFniOpen(Filename *selection, achar *filter, achar *title, Filename *initialDirectory,
                     LegacyUlong previews, ChunkNumber sound)
{
    StudioPortfolio::Request request;
    request.open = true;
    request.filter = filter;
    request.title = title;
    request.initialDirectory = initialDirectory;
    request.previews = previews;
    request.sound = sound;
    return StudioPortfolio::Choose(selection, request);
}
bool FPortGetFniSave(Filename *selection, achar *filter, achar *title, achar *defaultExtension,
                     PString defaultName, LegacyUlong previews, ChunkNumber sound)
{
    StudioPortfolio::Request request;
    request.open = false;
    request.filter = filter;
    request.title = title;
    request.defaultExtension = defaultExtension;
    request.defaultName = defaultName;
    request.previews = previews;
    request.sound = sound;
    return StudioPortfolio::Choose(selection, request);
}
