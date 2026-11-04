/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Standard dialogs.

***************************************************************************/
#include "dialog_resource.h"
#ifdef KAUAI_SDL3
#include "dialog_session.h"
#include "dialog_resources.h"
#endif
#include "frame.h"
ASSERTNAME
using HDLG = HWND;

// dialog init structure
struct DLGI
{
    PDialog pdlg;
    long iditFocus;
};

class NativeDialogAdapter
{
  public:
    static INT_PTR CALLBACK Dispatch(HWND hdlg, UINT msg, WPARAM w, LPARAM lw);
};

achar _szDlgProp[] = PszLit("Dialog");

/***************************************************************************
    Load resource bytes through the native adapter; parsing and logical item
    grouping are shared with future SDL dialog hosts.
***************************************************************************/
bool Dialog::_FReadResource(KauaiDialogs::Resource &model)
{
#ifdef KAUAI_SDL3
    if (KauaiDialogs::ReadApplicationResource(_rid, model)) return true;
#endif
    auto resource = FindResource(vwig.hinst, MIR(_rid), RT_DIALOG);
    auto loaded = resource ? LoadResource(vwig.hinst, resource) : nullptr;
    const auto bytes = loaded ? LockResource(loaded) : nullptr;
    const auto count = resource ? SizeofResource(vwig.hinst, resource) : 0;
    if (!bytes || !KauaiDialogs::ReadResource(bytes, count, model))
    {
        PushErc(ercDlgCantFind);
        return false;
    }
    return true;
}
bool Dialog::_FInit(void)
{
    KauaiDialogs::Resource model;
    return _FReadResource(model) && _FInitItems(model);
}

/***************************************************************************
    Windows dialog proc. Returns INT_PTR (= 4 bytes on x86, 8 bytes on Win64),
    matching the DLGPROC typedef. The return values used here (fTrue/fFalse,
    item indices) all fit in the low 32 bits regardless of architecture.
***************************************************************************/
INT_PTR CALLBACK NativeDialogAdapter::Dispatch(HWND hdlg, UINT msg, WPARAM w, LPARAM lw)
{
    PDialog pdlg;
    DialogItem dit;
    long idit;
    RC rcDlg;
    RC rcDsp;

    // this may return nil
    pdlg = (PDialog)GetProp(hdlg, _szDlgProp);

    switch (msg)
    {
    case WM_SYSCOMMAND:
        if (w == SC_SCREENSAVE && pvNil != vpappb && !vpappb->FAllowScreenSaver())
        {
            return fTrue;
        }
        break;

    case WM_INITDIALOG:
        DLGI *pdlgi;
        SystemRectangle rcs;

        // the pdlgi should be in the lParam
        pdlgi = (DLGI *)lw;
        pdlg = pdlgi->pdlg;
        AssertPo(pdlg, 0);

        // set the Dialog property so we can find the pdlg easily
        if (!SetProp(hdlg, _szDlgProp, (HANDLE)pdlg))
            goto LFail;

        // create a timer so we can do idle processing
        if (SetTimer(hdlg, reinterpret_cast<UINT_PTR>(hdlg), 10, pvNil) == 0)
            goto LFail;

        // create a container gob and attach the hdlg
        pdlg->_pgob = NewObj GraphicsObject(khidDialog);
        if (pdlg->_pgob == pvNil)
            goto LFail;
        if (!pdlg->_pgob->FAttachHwnd((HWND)hdlg))
        {
        LFail:
            PushErc(ercDlgOom);
            idit = ivNil;
            goto LEndDialog;
        }

        // set the dialog values
        pdlg->SetValues(0, pdlg->IvMac());
        if (ivNil != pdlgi->iditFocus)
            pdlg->SelectDit(pdlgi->iditFocus);

        GetWindowRect(hdlg, &rcs);
        rcDlg = rcs;

        rcDsp.Set(0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        if (hNil != vwig.hwndApp)
        {
            RC rcApp;

            GetWindowRect(vwig.hwndApp, &rcs);
            rcApp = rcs;
            rcDlg.CenterOnRc(&rcApp);
        }
        else
            rcDlg.CenterOnRc(&rcDsp);

        rcDlg.PinToRc(&rcDsp);
        MoveWindow(hdlg, rcDlg.xpLeft, rcDlg.ypTop, rcDlg.Dxp(), rcDlg.Dyp(), fFalse);

        return ivNil == pdlgi->iditFocus;

    case WM_TIMER:
        if (pvNil == pdlg)
            break;

        idit = ivNil;
        if (pdlg->_FDitChange(&idit))
            goto LEndDialog;
        break;

    case WM_COMMAND:
        if (pvNil == pdlg)
            break;

        if ((idit = pdlg->IditFromSit(GET_WM_COMMAND_ID(w, lw))) == ivNil)
            break;

        pdlg->GetDit(idit, &dit);
        switch (dit.ditk)
        {
        default:
            return fFalse;

        case ditkEditText:
            if (SwHigh(w) != EN_CHANGE)
                return fFalse;
            break;

        case ditkButton:
        case ditkCheckBox:
        case ditkRadioGroup:
            break;
        }

        if (pdlg->_FDitChange(&idit))
        {
        LEndDialog:
            if (pdlg->_pgob != pvNil)
            {
                if (idit != ivNil && !pdlg->FGetValues(0, pdlg->IvMac()))
                    idit = ivNil;
                pdlg->_pgob->FAttachHwnd(hNil);
                pdlg->_pgob->Release();
                pdlg->_pgob = pvNil;
            }
            else
                idit = ivNil;

            // remove the pdlg property and kill the timer
            RemoveProp(hdlg, _szDlgProp);
            KillTimer(hdlg, reinterpret_cast<UINT_PTR>(hdlg));

            EndDialog(hdlg, idit);
            return fTrue;
        }

        break;
    }

    return fFalse;
}

/***************************************************************************
    Actually put up the dialog and don't return until it comes down.
    Returns the idit that dismissed the dialog.  Returns ivNil on failure.
***************************************************************************/
long Dialog::IditDo(long iditFocus)
{
#ifdef KAUAI_SDL3
    LegacyLong result;
    if (_FDoSDL(iditFocus,result)) return result;
#endif
    long idit;
    DLGI dlgi;

    dlgi.pdlg = this;
    dlgi.iditFocus = iditFocus;
    idit = (long)DialogBoxParam(vwig.hinst, MIR(_rid), vwig.hwndApp, &NativeDialogAdapter::Dispatch, (LPARAM)&dlgi);

    return idit;
}

/***************************************************************************
    Make the given item the "focused" item and select its contents.  The
    item should be a text item or combo item.
***************************************************************************/
void Dialog::SelectDit(long idit)
{
#ifdef KAUAI_SDL3
    if (_session) { _session->focus=idit; return; }
#endif
    HDLG hdlg;
    DialogItem dit;

    if (pvNil == _pgob || hNil == (hdlg = (HDLG)_pgob->Hwnd()))
        goto LBug;

    GetDit(idit, &dit);
    if (dit.ditk != ditkEditText && dit.ditk != ditkCombo)
    {
    LBug:
        Bug("bad call to Dialog::SelectDit");
        return;
    }
    Assert(dit.sitLim == dit.sitMin + 1, "wrong lim on edit item");
    SetFocus(GetDlgItem(hdlg, dit.sitMin));
    SendDlgItemMessage(hdlg, dit.sitMin, EM_SETSEL, GET_EM_SETSEL_MPS(0, -1));
}

/***************************************************************************
    Get the value of a radio group.
***************************************************************************/
long Dialog::_LwGetRadioGroup(long idit)
{
    HDLG hdlg;
    DialogItem dit;
    long sit;

    GetDit(idit, &dit);
    hdlg = (HDLG)_pgob->Hwnd();
    Assert(hdlg != hNil, "no dialog!");
    Assert(dit.ditk == ditkRadioGroup, "not a radio group!");

    for (sit = dit.sitMin; sit < dit.sitLim; sit++)
    {
        if (IsDlgButtonChecked(hdlg, sit))
            return sit - dit.sitMin;
    }
    Bug("no radio button set");
    return dit.sitLim - dit.sitMin;
}

/***************************************************************************
    Change a radio group value.
***************************************************************************/
void Dialog::_SetRadioGroup(long idit, long lw)
{
    HDLG hdlg;
    DialogItem dit;

    GetDit(idit, &dit);
    hdlg = (HDLG)_pgob->Hwnd();
    Assert(hdlg != hNil, "no dialog!");
    Assert(dit.ditk == ditkRadioGroup, "not a radio group!");
    AssertIn(lw, 0, dit.sitLim - dit.sitMin);

    CheckRadioButton(hdlg, dit.sitMin, dit.sitLim - 1, dit.sitMin + lw);
}

/***************************************************************************
    Returns the current value of a check box.
***************************************************************************/
bool Dialog::_FGetCheckBox(long idit)
{
    HDLG hdlg;
    DialogItem dit;

    GetDit(idit, &dit);
    hdlg = (HDLG)_pgob->Hwnd();
    Assert(hdlg != hNil, "no dialog!");
    Assert(dit.ditk == ditkCheckBox, "not a check box!");
    Assert(dit.sitLim == dit.sitMin + 1, "wrong lim on check box");

    return FPure(IsDlgButtonChecked(hdlg, dit.sitMin));
}

/***************************************************************************
    Invert the value of a check box.
***************************************************************************/
void Dialog::_InvertCheckBox(long idit)
{
    _SetCheckBox(idit, !_FGetCheckBox(idit));
}

/***************************************************************************
    Set the value of a check box.
***************************************************************************/
void Dialog::_SetCheckBox(long idit, bool fOn)
{
    HDLG hdlg;
    DialogItem dit;

    GetDit(idit, &dit);
    hdlg = (HDLG)_pgob->Hwnd();
    Assert(hdlg != hNil, "no dialog!");
    Assert(dit.ditk == ditkCheckBox, "not a check box!");
    Assert(dit.sitLim == dit.sitMin + 1, "wrong lim on check box");

    CheckDlgButton(hdlg, dit.sitMin, FPure(fOn));
}

/***************************************************************************
    Get the text from an edit control or combo.
***************************************************************************/
void Dialog::_GetEditText(long idit, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    HDLG hdlg;
    DialogItem dit;
    ZString sz;

    GetDit(idit, &dit);
    hdlg = (HDLG)_pgob->Hwnd();
    Assert(hdlg != hNil, "no dialog!");
    Assert(dit.ditk == ditkEditText || dit.ditk == ditkCombo, "not edit item or combo!");
    Assert(dit.sitLim == dit.sitMin + 1, "wrong lim on item");

    GetDlgItemText(hdlg, dit.sitMin, sz, kcchMaxSz);
    *pstn = sz;
}

/***************************************************************************
    Set the text in an edit control or combo.
***************************************************************************/
void Dialog::_SetEditText(long idit, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    HDLG hdlg;
    DialogItem dit;

    GetDit(idit, &dit);
    hdlg = (HDLG)_pgob->Hwnd();
    Assert(hdlg != hNil, "no dialog!");
    Assert(dit.ditk == ditkEditText || dit.ditk == ditkCombo, "not edit item or combo!");
    Assert(dit.sitLim == dit.sitMin + 1, "wrong lim on item");

    SetDlgItemText(hdlg, dit.sitMin, pstn->Psz());
}

/***************************************************************************
    Add a string to a combo item.
***************************************************************************/
bool Dialog::_FAddToList(long idit, PString pstn)
{
    AssertThis(0);
    AssertPo(pstn, 0);
    HDLG hdlg;
    HWND hwndCombo;
    DialogItem dit;

    GetDit(idit, &dit);
    hdlg = (HDLG)_pgob->Hwnd();
    Assert(hdlg != hNil, "no dialog!");
    Assert(dit.ditk == ditkCombo, "not combo!");
    Assert(dit.sitLim == dit.sitMin + 1, "wrong lim on item");

    if (hNil == (hwndCombo = GetDlgItem(hdlg, dit.sitMin)))
    {
        Warn("Couldn't get combo hwnd");
        return fFalse;
    }

    return 0 <= (long)SendMessage(hwndCombo, CB_ADDSTRING, 0, (LPARAM)pstn->Psz());
}

/***************************************************************************
    Empty the list portion of the combo item.
***************************************************************************/
void Dialog::_ClearList(long idit)
{
    AssertThis(0);
    HDLG hdlg;
    HWND hwndCombo;
    DialogItem dit;

    GetDit(idit, &dit);
    hdlg = (HDLG)_pgob->Hwnd();
    Assert(hdlg != hNil, "no dialog!");
    Assert(dit.ditk == ditkCombo, "not combo!");
    Assert(dit.sitLim == dit.sitMin + 1, "wrong lim on item");

    if (hNil == (hwndCombo = GetDlgItem(hdlg, dit.sitMin)))
    {
        Warn("Couldn't get combo hwnd");
        return;
    }

    SendMessage(hwndCombo, CB_RESETCONTENT, 0, 0);
}
