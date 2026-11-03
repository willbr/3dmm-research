/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_TEXT_UTF8_H
#define KAUAI_TEXT_UTF8_H
#include <cstdint>
#include <string>
#include <vector>
namespace KauaiText
{
// Strict counted UTF-8/native-unit UTF-16 conversion. Empty text and embedded
// NULs are supported. Output is cleared first and may be partial on failure.
// Rejects overlong sequences, invalid scalars and unpaired surrogates.
bool EncodeUtf8(const std::vector<std::uint16_t> &units, std::string &result);
bool DecodeUtf8(const std::string &text, std::vector<std::uint16_t> &units);
} // namespace KauaiText
#endif
