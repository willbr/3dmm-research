/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_CONTENT_H
#define STUDIO_CONTENT_H
#include "platform_files.h"
#include <string>
#include <vector>
namespace StudioContent
{
using Char = KauaiFiles::PathChar;
using Text = std::basic_string<Char>;
enum class Status
{
    Ready,
    Invalid,
    Error
};
struct Product
{
    Text sourceId, title; // Title retains the legacy long-name/short-name spelling.
};
struct Products
{
    Status status = Status::Ready;
    std::vector<Product> entries;
    std::uint32_t error = 0; // Native diagnostic code on I/O failure.
};
struct Root
{
    Status status = Status::Ready;
    Text path; // Empty means no override; never means the current directory.
    std::uint32_t error = 0;
};
// Optional THREEDMM_CONTENT_ROOT, in native path code units. Must be absolute,
// at most 255 code units, and name the parent of the product directories.
// The caller verifies the installed content; this function does no I/O.
Root ConfiguredRoot();
// Windows reads the original HKLM Products key in the 32-bit registry view.
// POSIX reads <root>/products.tsv; a missing file means an empty list and lets
// Studio apply its existing base-product fallback. Other errors are failures.
Products ReadProducts(const Char *root);
// Portable manifest: source-id TAB long-name/short-name, one row per line.
// LF/CRLF, blank lines and # comments accepted; no quoting or escaping.
// Fields are opaque native bytes, <=255 each. Studio retains SID parsing.
// Malformed input fails atomically. Maximum file size is 1 MiB.
Products ParseProducts(const std::string &bytes);
Root ValidateRoot(Text path, bool windows);
} // namespace StudioContent
#endif
