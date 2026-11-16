/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEXT_BOX_UNDO_H
#define MM_ENGINE_TEXT_BOX_UNDO_H
#include "text_box_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Type/size/lifetime/color replay swaps its saved value at the original stage.
 * Initial switch/seek/missing-box failures flush then clear movie undo; clear
 * may destroy this record, so C performs no later state/owner access.
 * Wrapped edit false is returned without that failure cleanup. Exceptions keep
 * the completed prefix and do not take ordinary-failure branches. */
int mm_text_box_undo_replay(MMTextBoxUndo *, int undo, const MMTextBoxEditServices *);
int mm_text_box_undo_dispose(MMTextBoxUndo *, const MMTextBoxEditServices *);
#ifdef __cplusplus
}
#endif
#endif
