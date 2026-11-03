/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SETTINGS_STORE_WIN_H
#define KAUAI_SETTINGS_STORE_WIN_H
#include "settings_store.h"
namespace KauaiSettings
{
// Private Windows adapter: original HKCU/HKLM keys and 32-bit registry view.
const Backend &WindowsBackend();
} // namespace KauaiSettings
#endif
