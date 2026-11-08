/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SETTINGS_STORE_WIN_H
#define KAUAI_SETTINGS_STORE_WIN_H
#include "settings_store.h"
#include <string>
#include <vector>
namespace KauaiSettings
{
// Private Windows adapter: original HKCU/HKLM keys and 32-bit registry view.
const Backend &WindowsBackend();
struct WindowsValue
{
    std::basic_string<NameChar> name;
    Type type;
    std::vector<unsigned char> bytes;
};
// Read-only snapshot for migration; a missing key is an empty successful result.
bool ReadWindowsValues(const NameChar *key, bool machine, std::vector<WindowsValue> &values);
} // namespace KauaiSettings
#endif
