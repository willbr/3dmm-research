/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Standard dialogs.

***************************************************************************/
#include <new>
#include "dialog_resource.h"
#include "dialog_session.h"
#include "platform_text.h"
#include "text_utf8.h"
#include "kauai_core.h"
#include "dlg.h"

ASSERTNAME

RTCLASS(Dialog)

/***************************************************************************
    Constructor for a dialog object.
***************************************************************************/
Dialog::Dialog(LegacyLong rid) : GeneralGroup(sizeof(DialogItem))
{
    _rid = rid;
    _session = nullptr;
}

Dialog::~Dialog()
{
    delete _tabs;
}

bool Dialog::FEnableControl(LegacyLong sit, bool enabled)
{
    if (!_session)
        return false;
    for (auto &control : _session->resource.controls)
        if (control.id == static_cast<std::uint32_t>(sit))
        {
            if (enabled)
                control.style &= ~0x08000000u;
            else
                control.style |= 0x08000000u;
            return true;
        }
    return false;
}

bool Dialog::FSetTabs(const KauaiDialogs::TabLayout &layout)
{
    auto tabs = new (std::nothrow) KauaiDialogs::TabLayout;
    if (!tabs)
        return false;
    try
    {
        *tabs = layout;
    }
    catch (...)
    {
        delete tabs;
        return false;
    }
    delete _tabs;
    _tabs = tabs;
    return true;
}

bool Dialog::_FInitItems(const KauaiDialogs::Resource &resource)
{
    std::vector<KauaiDialogs::Binding> bindings;
    if (!KauaiDialogs::BuildBindings(resource, bindings) || bindings.empty())
    {
        PushErc(ercDlgCantFind);
        return false;
    }
    static_assert(static_cast<int>(KauaiDialogs::ItemKind::button) == ditkButton &&
                      static_cast<int>(KauaiDialogs::ItemKind::checkBox) == ditkCheckBox &&
                      static_cast<int>(KauaiDialogs::ItemKind::radioGroup) == ditkRadioGroup &&
                      static_cast<int>(KauaiDialogs::ItemKind::editText) == ditkEditText &&
                      static_cast<int>(KauaiDialogs::ItemKind::combo) == ditkCombo,
                  "dialog item kind drift");
    if (!FEnsureSpace(static_cast<LegacyLong>((bindings.size)()), sizeof(LegacyLong), fgrpNil))
    {
        PushErc(ercDlgOom);
        return false;
    }
    for (const auto &binding : bindings)
    {
        // The legacy callback API uses signed 32-bit half-open item ranges.
        if (binding.limit > INT32_MAX)
        {
            PushErc(ercDlgCantFind);
            return false;
        }
        DialogItem item = {static_cast<LegacyLong>(binding.first), static_cast<LegacyLong>(binding.limit),
                           static_cast<LegacyLong>(binding.kind)};
        const LegacyLong extra =
            binding.kind == KauaiDialogs::ItemKind::checkBox || binding.kind == KauaiDialogs::ItemKind::radioGroup
                ? sizeof(LegacyLong)
                : 0;
        if (!FInsert(IvMac(), extra, nullptr, &item))
        {
            PushErc(ercDlgOom);
            return false;
        }
        if (extra)
            ClearPb(QvGet(IvMac() - 1), extra);
    }
    FEnsureSpace(0, 0, fgrpShrink);
    return true;
}

bool Dialog::_FSessionText(LegacyLong item, PString text, bool read, bool append)
{
    auto &value = _session->values[item];
    std::vector<std::uint16_t> units;
    if (read)
    {
        if (!KauaiText::DecodeUtf8(value.text, units))
            return false;
#ifdef UNICODE
        text->SetRgch(reinterpret_cast<achar *>(units.data()),
                      LwMin(static_cast<LegacyLong>((units.size)()), kcchMaxStn));
#else
        const auto source = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
        const auto count = KauaiText::ToAnsi(units.data(), (units.size)() * 2, source, nullptr, 0);
        if (!(units.empty)() && !count)
            return false;
        std::vector<char> bytes(count);
        if (count && !KauaiText::ToAnsi(units.data(), (units.size)() * 2, source, bytes.data(), count))
            return false;
        text->SetRgch(bytes.data(), LwMin(static_cast<LegacyLong>(count), kcchMaxStn));
#endif
        return true;
    }
#ifdef UNICODE
    const auto encoding = BigLittle(KauaiText::Encoding::Utf16BE, KauaiText::Encoding::Utf16LE);
#else
    const auto encoding = KauaiText::Encoding::WindowsAnsi;
#endif
    const auto count = KauaiText::ToUtf16(text->Prgch(), text->Cch() * sizeof(achar), encoding, nullptr, 0);
    if (text->Cch() && !count)
        return false;
    units.resize(count);
    if (count && !KauaiText::ToUtf16(text->Prgch(), text->Cch() * sizeof(achar), encoding, units.data(), count))
        return false;
    std::string utf8;
    if (!KauaiText::EncodeUtf8(units, utf8))
        return false;
    if (append)
        value.choices.push_back(std::move(utf8));
    else
        value.text = std::move(utf8);
    return true;
}

/***************************************************************************
    Static method to create a new Dialog.  Does NewObj then calls _FInit.
***************************************************************************/
PDialog Dialog::PdlgNew(LegacyLong rid, PFNDLG pfn, void *pv)
{
    PDialog pdlg;

    if ((pdlg = NewObj Dialog(rid)) == pvNil)
        return pvNil;

    pdlg->_pfn = pfn;
    pdlg->_pv = pv;

    if (!pdlg->_FInit())
        ReleasePpo(&pdlg);

    return pdlg;
}

/***************************************************************************
    Get the values for [iditMin, iditLim) from the actual dialog and put
    them in the GGDIT.
***************************************************************************/
bool Dialog::FGetValues(LegacyLong iditMin, LegacyLong iditLim)
{
    AssertThis(0);
    LegacyLong idit;
    DialogItem dit;
    LegacyLong lw;
    String stn;

    AssertIn(iditMin, 0, iditLim);
    if (_pgob == pvNil && !_session)
    {
        Bug("why are you calling this when the dialog doesn't exist?");
        return fFalse;
    }

    iditLim = LwMin(iditLim, IvMac());
    for (idit = iditMin; idit < iditLim; idit++)
    {
        GetDit(idit, &dit);
        switch (dit.ditk)
        {
        case ditkCheckBox:
            lw = _session ? _session->values[idit].number != 0 : _FGetCheckBox(idit);
            goto LPutLw;

        case ditkRadioGroup:
            lw = _session ? _session->values[idit].number : _LwGetRadioGroup(idit);
        LPutLw:
            PutRgb(idit, 0, sizeof(lw), &lw);
            break;

        case ditkEditText:
        case ditkCombo:
            if (_session) { if (!_FSessionText(idit,&stn,true)) return false; }
            else _GetEditText(idit, &stn);
            if (!FPutStn(idit, &stn))
                return fFalse;
            break;
        }
    }

    return fTrue;
}

/***************************************************************************
    Set the values for [iditMin, iditLim) from the GGDIT into the actual
    dialog.
***************************************************************************/
void Dialog::SetValues(LegacyLong iditMin, LegacyLong iditLim)
{
    AssertThis(0);
    LegacyLong idit;
    DialogItem dit;
    String stn;
    LegacyLong lw;
    LegacyLong cb, cbT, ib;
    byte *prgb;

    if (_pgob == pvNil && !_session)
    {
        Bug("why are you calling this when the dialog doesn't exist?");
        return;
    }

    iditLim = LwMin(iditLim, IvMac());
    for (idit = iditMin; idit < iditLim; idit++)
    {
        GetDit(idit, &dit);
        switch (dit.ditk)
        {
        case ditkCheckBox:
            GetRgb(idit, 0, sizeof(lw), &lw);
            if (_session)
                _session->values[idit].number = FPure(lw);
            else
                _SetCheckBox(idit, lw);
            break;

        case ditkRadioGroup:
            GetRgb(idit, 0, sizeof(lw), &lw);
            if (_session)
                _session->values[idit].number = lw;
            else
                _SetRadioGroup(idit, lw);
            break;

        case ditkEditText:
            GetStn(idit, &stn);
            if (_session)
                _FSessionText(idit, &stn, false);
            else
                _SetEditText(idit, &stn);
            break;

        case ditkCombo:
            if (_session)
                _session->values[idit].choices.clear();
            else
                _ClearList(idit);
            cb = Cb(idit);
            if (cb <= 0)
            {
                stn.SetNil();
                if (_session)
                    _FSessionText(idit, &stn, false);
                else
                    _SetEditText(idit, &stn);
                break;
            }
            prgb = (byte *)PvLock(idit);
            if (!stn.FSetData(prgb, cb, &cbT))
            {
                Bug("bad combo item");
                cbT = cb;
            }
            if (_session)
                _FSessionText(idit, &stn, false);
            else
                _SetEditText(idit, &stn);
            for (ib = cbT; ib < cb;)
            {
                if (!stn.FSetData(prgb + ib, cb - ib, &cbT))
                {
                    BugVar("bad combo item", &ib);
                    break;
                }
                ib += cbT;
                if (_session)
                    _FSessionText(idit, &stn, false, true);
                else
                    _FAddToList(idit, &stn);
            }
            Unlock();
            break;
        }
    }
}

/***************************************************************************
    Get the item number from a system item number.
***************************************************************************/
LegacyLong Dialog::IditFromSit(LegacyLong sit)
{
    LegacyLong idit;
    DialogItem dit;

    for (idit = IvMac(); idit-- != 0;)
    {
        GetDit(idit, &dit);
        if (sit >= dit.sitMin && sit < dit.sitLim)
            return idit;
    }
    return ivNil;
}

/***************************************************************************
    Calls the PFNDLG (if not nil) to notify of a change.  PFNDLG should
    return true if the dialog should be dismissed.  The PFNDLG is free
    to change *pidit.  If a nil PFNDLG was specified (in PdlgNew),
    this returns true (dismisses the dialog) on any button hit.
***************************************************************************/
bool Dialog::_FDitChange(LegacyLong *pidit)
{
    if (pvNil == _pfn)
    {
        DialogItem dit;

        if (ivNil == *pidit)
            return fFalse;

        GetDit(*pidit, &dit);
        return dit.ditk == ditkButton;
    }

    return (*_pfn)(this, pidit, _pv);
}

/***************************************************************************
    Get the stn (for an edit item).
***************************************************************************/
void Dialog::GetStn(LegacyLong idit, PString pstn)
{
    AssertThis(0);
    AssertIn(idit, 0, IvMac());
    AssertPo(pstn, 0);
    LegacyLong cb;

#ifdef DEBUG
    DialogItem dit;
    GetDit(idit, &dit);
    Assert(ditkEditText == dit.ditk || dit.ditk == ditkCombo, "not a text item or combo");
#endif // DEBUG

    cb = Cb(idit);
    if (cb <= 0)
        pstn->SetNil();
    else
    {
        AssertDo(pstn->FSetData(PvLock(idit), cb, &cb), 0);
        Unlock();
    }
}

/***************************************************************************
    Put the stn into the Dialog.
***************************************************************************/
bool Dialog::FPutStn(LegacyLong idit, PString pstn)
{
    AssertThis(0);
    AssertIn(idit, 0, IvMac());
    AssertPo(pstn, 0);
    DialogItem dit;
    LegacyLong cbOld, cbNew;

    GetDit(idit, &dit);
    cbOld = Cb(idit);
    cbNew = pstn->CbData();
    switch (dit.ditk)
    {
    default:
        Bug("not a text item or combo");
        return fFalse;

    case ditkEditText:
        if (cbOld != cbNew && !FPut(idit, cbNew, pvNil))
            return fFalse;
        break;

    case ditkCombo:
        if (cbOld > 0)
        {
            String stn;

            if (!stn.FSetData(PvLock(idit), cbOld, &cbOld))
            {
                Bug("why did setting the data fail?");
                cbOld = Cb(idit);
            }
            Unlock();
        }
        if (cbOld > cbNew)
            DeleteRgb(idit, 0, cbOld - cbNew);
        else if (cbOld < cbNew && !FInsertRgb(idit, 0, cbNew - cbOld, pvNil))
            return fFalse;
        break;
    }

    pstn->GetData(PvLock(idit));
    Unlock();
    return fTrue;
}

/***************************************************************************
    Get the value of a radio group.
***************************************************************************/
LegacyLong Dialog::LwGetRadio(LegacyLong idit)
{
    AssertThis(0);
    AssertIn(idit, 0, IvMac());
    LegacyLong lw;

#ifdef DEBUG
    DialogItem dit;
    GetDit(idit, &dit);
    Assert(ditkRadioGroup == dit.ditk, "not a radio group");
#endif // DEBUG

    GetRgb(idit, 0, sizeof(LegacyLong), &lw);
    return lw;
}

/***************************************************************************
    Set the value of the radio group.
***************************************************************************/
void Dialog::PutRadio(LegacyLong idit, LegacyLong lw)
{
    AssertThis(0);
    AssertIn(idit, 0, IvMac());

#ifdef DEBUG
    DialogItem dit;
    GetDit(idit, &dit);
    Assert(ditkRadioGroup == dit.ditk, "not a radio group");
    AssertIn(lw, 0, dit.sitLim - dit.sitMin);
#endif // DEBUG

    PutRgb(idit, 0, sizeof(LegacyLong), &lw);
}

/***************************************************************************
    Get the value of a check box.
***************************************************************************/
bool Dialog::FGetCheck(LegacyLong idit)
{
    AssertThis(0);
    AssertIn(idit, 0, IvMac());
    LegacyLong lw;

#ifdef DEBUG
    DialogItem dit;
    GetDit(idit, &dit);
    Assert(ditkCheckBox == dit.ditk, "not a check box");
#endif // DEBUG

    GetRgb(idit, 0, sizeof(LegacyLong), &lw);
    return lw;
}

/***************************************************************************
    Set the value of a check box item.
***************************************************************************/
void Dialog::PutCheck(LegacyLong idit, bool fOn)
{
    AssertThis(0);
    AssertIn(idit, 0, IvMac());
    LegacyLong lw;

#ifdef DEBUG
    DialogItem dit;
    GetDit(idit, &dit);
    Assert(ditkCheckBox == dit.ditk, "not a check box");
#endif // DEBUG

    lw = FPure(fOn);
    PutRgb(idit, 0, sizeof(LegacyLong), &lw);
}

/***************************************************************************
    Get the indicated edit item from the dialog and convert it to a LegacyLong.
    If the string is empty, sets *plw to zero and sets *pfEmpty (if pfEmpty
    is not nil) and returns false.  If the string doesn't parse as a number,
    returns false.
***************************************************************************/
bool Dialog::FGetLwFromEdit(LegacyLong idit, LegacyLong *plw, bool *pfEmpty)
{
    AssertThis(0);
    AssertVarMem(plw);
    AssertNilOrVarMem(pfEmpty);
    String stn;

    GetStn(idit, &stn);
    if (0 == stn.Cch())
    {
        if (pvNil != pfEmpty)
            *pfEmpty = fTrue;
        *plw = 0;
        return fFalse;
    }
    if (pvNil != pfEmpty)
        *pfEmpty = fFalse;
    if (!stn.FGetLw(plw, 0))
    {
        TrashVar(plw);
        return fFalse;
    }
    return fTrue;
}

/***************************************************************************
    Put the LegacyLong into the indicated edit item (in decimal).
***************************************************************************/
bool Dialog::FPutLwInEdit(LegacyLong idit, LegacyLong lw)
{
    AssertThis(0);
    String stn;

    stn.FFormatSz(PszLit("%d"), lw);
    return FPutStn(idit, &stn);
}

/***************************************************************************
    Add the string to the given list item.
***************************************************************************/
bool Dialog::FAddToList(LegacyLong idit, PString pstn)
{
    AssertThis(0);
    LegacyLong cb, cbTot;

#ifdef DEBUG
    DialogItem dit;
    GetDit(idit, &dit);
    Assert(ditkCombo == dit.ditk, "not a combo");
#endif // DEBUG

    cbTot = Cb(idit);
    if (cbTot == 0)
    {
        String stn;

        stn.SetNil();
        if (!FPut(idit, cbTot = stn.CbData(), pvNil))
            return fFalse;
        stn.GetData(PvLock(idit));
        Unlock();
    }
    cb = pstn->CbData();
    if (!FInsertRgb(idit, cbTot, cb, pvNil))
        return fFalse;

    pstn->GetData(PvAddBv(PvLock(idit), cbTot));
    Unlock();
    return fTrue;
}

/***************************************************************************
    Empty the list of options for the list item.
***************************************************************************/
void Dialog::ClearList(LegacyLong idit)
{
    AssertThis(0);
    AssertIn(idit, 0, IvMac());
    LegacyLong cbOld, cbNew;
    String stn;

#ifdef DEBUG
    DialogItem dit;
    GetDit(idit, &dit);
    Assert(ditkCombo == dit.ditk, "not a combo");
#endif // DEBUG

    cbOld = Cb(idit);
    if (cbOld <= 0)
        return;

    if (!stn.FSetData(PvLock(idit), cbOld, &cbNew))
    {
        Bug("why did setting the data fail?");
        cbNew = 0;
    }
    Unlock();
    if (cbOld > cbNew)
        DeleteRgb(idit, cbNew, cbOld - cbNew);
}
