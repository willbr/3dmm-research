/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Dialog header file.

***************************************************************************/
#ifndef DLG_H
#define DLG_H
#include "kauai_core.h"

namespace KauaiDialogs
{
struct Resource;
struct Session;
struct TabLayout;
} // namespace KauaiDialogs

using Group::GeneralGroup;

// The native modal adapter owns the container. Shared item/value code only
// checks whether a session is active and needs no graphics or SDK definition.
class GraphicsObject;
typedef GraphicsObject *PGraphicsObject;

// type of dialog item
enum
{
    ditkButton,     // no value
    ditkCheckBox,   // LegacyLong (bool)
    ditkRadioGroup, // LegacyLong (index)
    ditkEditText,   // streamed stn
    ditkCombo,      // streamed stn, followed by a list of streamed stn's.
    ditkLim
};

// dialog item
struct DialogItem
{
    LegacyLong sitMin; // first system item number (for this DialogItem)
    LegacyLong sitLim; // lim of system item numbers (for this DialogItem)
    LegacyLong ditk;   // kind of item
};

typedef class Dialog *PDialog;

// callback to notify of an item change (while the dialog is active)
typedef bool (*PFNDLG)(PDialog pdlg, LegacyLong *pidit, void *pv);

// dialog class - a Dialog is a GeneralGroup of DITs
#define Dialog_PAR GeneralGroup
#define kclsDialog 'DLG'
class Dialog : public Dialog_PAR
{
    RTCLASS_DEC

  private:
    PGraphicsObject _pgob;
    KauaiDialogs::Session *_session;
    LegacyLong _rid;
    PFNDLG _pfn;
    void *_pv;
    KauaiDialogs::TabLayout *_tabs = nullptr;

    friend class NativeDialogAdapter;

    Dialog(LegacyLong rid);
    bool _FInit(void);
    bool _FReadResource(KauaiDialogs::Resource &resource);
    bool _FDoSDL(LegacyLong focus, LegacyLong &result);
    bool _FSessionText(LegacyLong item, PString text, bool read, bool append = false);
    bool _FInitItems(const KauaiDialogs::Resource &resource);

    LegacyLong _LwGetRadioGroup(LegacyLong idit);
    void _SetRadioGroup(LegacyLong idit, LegacyLong lw);
    bool _FGetCheckBox(LegacyLong idit);
    void _InvertCheckBox(LegacyLong idit);
    void _SetCheckBox(LegacyLong idit, bool fOn);
    void _GetEditText(LegacyLong idit, PString pstn);
    void _SetEditText(LegacyLong idit, PString pstn);
    bool _FDitChange(LegacyLong *pidit);
    bool _FAddToList(LegacyLong idit, PString pstn);
    void _ClearList(LegacyLong idit);

  public:
    static PDialog PdlgNew(LegacyLong rid, PFNDLG pfn = pvNil, void *pv = pvNil);
    ~Dialog();
    bool FSetTabs(const KauaiDialogs::TabLayout &layout);

    LegacyLong IditDo(LegacyLong iditFocus = ivNil);

    // these are only valid while the dialog is up
    bool FGetValues(LegacyLong iditMin, LegacyLong iditLim);
    void SetValues(LegacyLong iditMin, LegacyLong iditLim);
    void SelectDit(LegacyLong idit);

    // argument access
    LegacyLong IditFromSit(LegacyLong sit);
    void GetDit(LegacyLong idit, DialogItem *pdit)
    {
        GetFixed(idit, pdit);
    }
    void PutDit(LegacyLong idit, DialogItem *pdit)
    {
        PutFixed(idit, pdit);
    }

    void GetStn(LegacyLong idit, PString pstn);
    bool FPutStn(LegacyLong idit, PString pstn);
    LegacyLong LwGetRadio(LegacyLong idit);
    void PutRadio(LegacyLong idit, LegacyLong lw);
    bool FGetCheck(LegacyLong idit);
    void PutCheck(LegacyLong idit, bool fOn);

    bool FGetLwFromEdit(LegacyLong idit, LegacyLong *plw, bool *pfEmpty = pvNil);
    bool FPutLwInEdit(LegacyLong idit, LegacyLong lw);

    bool FAddToList(LegacyLong idit, PString pstn);
    void ClearList(LegacyLong idit);
};

#endif //! DLG_H
