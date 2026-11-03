/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dlg.h"
#undef size
#include "dialog_resource.h"
#include <type_traits>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared dialog API must not import the Windows SDK
#endif
static_assert(sizeof(DialogItem) == 12, "dialog item widths must remain 32 bits");
using ReadNumber = bool (Dialog::*)(LegacyLong, LegacyLong *, bool *);
static_assert(std::is_same<decltype(&Dialog::FGetLwFromEdit), ReadNumber>::value, "dialog value API drift");
static_assert(static_cast<int>(KauaiDialogs::ItemKind::combo) == ditkCombo, "dialog binding kind drift");
