/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Reviewed:
    Copyright (c) Microsoft Corporation

    Declarations related to key handling

***************************************************************************/
#ifndef KEYS_H
#define KEYS_H

#define vkNil 0

// Framework key codes, also emitted by SDL. No native SDK is needed here.
#define kvkLeft MacWin(0x1C, 0x25)
#define kvkRight MacWin(0x1D, 0x27)
#define kvkUp MacWin(0x1E, 0x26)
#define kvkDown MacWin(0x1F, 0x28)
#define kvkHome MacWin(0x01, 0x24)
#define kvkEnd MacWin(0x04, 0x23)
#define kvkPageUp MacWin(0x0B, 0x21)
#define kvkPageDown MacWin(0x0C, 0x22)

#define kvkBack MacWin(0x08, 0x08)
#define kvkDelete MacWin(0x7F, 0x2E)
#define kvkReturn MacWin(0x0D, 0x0D)
#define kvkEscape MacWin(0x1B, 0x1B)

#endif //! KEYS_H
