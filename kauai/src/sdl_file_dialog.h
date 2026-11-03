/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_FILE_DIALOG_H
#define KAUAI_SDL_FILE_DIALOG_H
#include "file_browser.h"
#include <cstdint>
namespace KauaiSDL
{
struct DialogImage
{
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels; // Top-down opaque 0xAARRGGBB.
};
struct FileDialog
{
    KauaiFiles::Browser browser;
    std::string title;
    DialogImage artwork, acceptButton, cancelButton, homeButton;
    void *context = nullptr;
    bool (*preview)(void *, const std::string &, DialogImage &) = nullptr;
    bool accepted = false;
};
// True once shown; accepted/selection distinguish selection from cancellation.
// A successful selection never opens or writes the chosen file.
bool RunFileDialog(FileDialog &, std::uint32_t parent = 0, void (*idle)(void *) = nullptr, void *context = nullptr);
} // namespace KauaiSDL
#endif
