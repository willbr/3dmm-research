/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "embedded_midi.h"
#include "compressed_wave.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#ifdef STUDIO_BUNDLED_MIDI
#include "studio_midi_licenses.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
extern "C"
{
    extern const unsigned char StudioSoundFontStart[], StudioSoundFontEnd[];
}
#endif
#endif
std::vector<StudioISO::Supplement> StudioISO::BundledMIDI()
{
#ifdef STUDIO_BUNDLED_MIDI
    const unsigned char *data;
    std::size_t bytes;
#ifdef _WIN32
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(30001), MAKEINTRESOURCEW(10));
    data = resource ? static_cast<const unsigned char *>(LockResource(LoadResource(module, resource))) : nullptr;
    bytes = resource ? SizeofResource(module, resource) : 0;
#else
    data = StudioSoundFontStart;
    bytes = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(StudioSoundFontEnd) -
                                     reinterpret_cast<std::uintptr_t>(StudioSoundFontStart));
#endif
    if (!data || !bytes)
        throw std::runtime_error("The bundled MIDI instrument bank is missing");
    std::vector<StudioISO::Supplement> result = {{"assets/GeneralUserGS.sf2", data, bytes},
            {"licenses/generaluser-gs-LICENSE.txt", reinterpret_cast<const unsigned char *>(StudioMIDIBankLicense),
             sizeof(StudioMIDIBankLicense) - 1},
            {"licenses/tinysoundfont-LICENSE.txt", reinterpret_cast<const unsigned char *>(StudioMIDICodeLicense),
             sizeof(StudioMIDICodeLicense) - 1},
            {"licenses/tinysoundfont-PATCHES.md", reinterpret_cast<const unsigned char *>(StudioMIDIPatches),
             sizeof(StudioMIDIPatches) - 1}};
#else
    std::vector<StudioISO::Supplement> result;
#endif
    const auto licenses = KauaiAudio::CompressedWaveLicenseText();
    result.push_back({"licenses/audio-codecs-LICENSE.txt", reinterpret_cast<const unsigned char *>(licenses),
                      std::strlen(licenses)});
    return result;
}
