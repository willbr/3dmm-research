/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_settings.h"
#ifdef _WIN32
#include "settings_store_win.h"
#define NOMINMAX
#include <windows.h>
#endif
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
using namespace KauaiSettings;
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "INI check failed at %d\n", __LINE__);                                                \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static std::basic_string<NameChar> Name(const char *text)
{
    std::basic_string<NameChar> value;
    while (*text)
        value += static_cast<NameChar>(*text++);
    return value;
}
static std::string Load()
{
    std::size_t bytes;
    std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile(StudioSettings::IniPath().c_str(), &bytes), SDL_free);
    return data ? std::string(static_cast<char *>(data.get()), bytes) : std::string();
}
int main()
{
#if !defined(DEBUG) && !defined(STUDIO_TEST_TOOLS)
    std::puts("INI isolation checks require Debug or STUDIO_TEST_TOOLS.");
    return 77;
#endif
    const auto run = "abc-" + std::to_string(SDL_GetPerformanceCounter());
#ifdef _WIN32
    _putenv_s("KAUAI_TEST_SETTINGS_RUN", run.c_str());
#else
    setenv("KAUAI_TEST_SETTINGS_RUN", run.c_str(), 1);
#endif
    const auto key = Name("Software\\Microsoft\\Microsoft Kids\\3D Movie Maker");
    const auto option = Name("AllowMultipleInstances"), number = Name("Count"), blob = Name("UserData"),
               folder = Name("HomeDir");
    bool enabled = true;
    Request request;
    request.type = Type::Binary;
    request.write = true;
#ifdef _WIN32
    CHECK(Access(WindowsBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request));
#endif
    request.write = false;
    request.setDefault = true;
    enabled = false;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request));
#ifdef _WIN32
    CHECK(enabled); // First open imports before set-default can overwrite it.
#else
    CHECK(!enabled);
#endif
    request.write = true;
    request.setDefault = false;
    enabled = false;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request));
#ifdef _WIN32
    request.write = false;
    CHECK(Access(WindowsBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request) && enabled);
    // Existing INI wins after the initial import, even when the registry changes.
    enabled = true;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request) &&
          !enabled);
#endif
    unsigned char bytes[] = {0, 255, 17, 0, 42}, copy[sizeof(bytes)] = {};
    request.write = true;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), blob.c_str(), bytes, sizeof(bytes), request));
    request.write = false;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), blob.c_str(), copy, sizeof(copy), request));
    CHECK(!std::memcmp(bytes, copy, sizeof(bytes)));
    std::uint32_t value = 4294967295U;
    request.type = Type::Number;
    request.machine = true;
    request.write = true;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), number.c_str(), &value, sizeof(value), request));
    value = 0;
    request.write = false;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), number.c_str(), &value, sizeof(value), request) &&
          value == UINT32_MAX);
    auto path = Name("C:\\My movies\\a \"quoted\" folder\nnext");
#if defined(UNICODE) || defined(_WIN32)
    path += static_cast<NameChar>(0xe9);
#else
    path += u8"\u00e9";
#endif
    request = {};
    request.type = Type::String;
    request.write = true;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), folder.c_str(), path.data(),
                 static_cast<std::uint32_t>((path.size() + 1) * sizeof(NameChar)), request));
    NameChar output[200] = {};
    request.write = false;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), folder.c_str(), output, sizeof(output), request));
    CHECK(path == output);
    const auto before = Load();
    const auto comments = before + "\n; keep my comment\n[Custom]\nExample=untouched\n";
    CHECK(SDL_SaveFile(StudioSettings::IniPath().c_str(), comments.data(), comments.size()));
    request = {};
    request.type = Type::Binary;
    request.write = true;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request));
    CHECK(Load().find("; keep my comment\n[Custom]\nExample=untouched") != std::string::npos);
    auto edited = Load();
    const auto position = edited.find("AllowMultipleInstances=0");
    CHECK(position != std::string::npos);
    edited[position + std::strlen("AllowMultipleInstances=")] = '1';
    CHECK(SDL_SaveFile(StudioSettings::IniPath().c_str(), edited.data(), edited.size()));
    request.write = false;
    CHECK(Access(StudioSettings::IniBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request) &&
          enabled);
    bool created = false;
    auto first = StudioSettings::IniBackend().open(key.c_str(), false, &created);
    CHECK(first && !created);
    CHECK(!StudioSettings::IniBackend().open(key.c_str(), false, &created));
    StudioSettings::IniBackend().close(first);
    // A failed temporary write cannot replace a good file.
    const auto temporary = std::filesystem::u8path(StudioSettings::IniPath() + ".tmp");
    CHECK(std::filesystem::create_directory(temporary));
    request.write = true;
    CHECK(!Access(StudioSettings::IniBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request));
    CHECK(Load() == edited);
    CHECK(std::filesystem::remove(temporary));
    const std::string bad = "[User]\nAllowMultipleInstances=not a number\n[User.Types]\nAllowMultipleInstances=byte\n";
    CHECK(SDL_SaveFile(StudioSettings::IniPath().c_str(), bad.data(), bad.size()));
    request.write = false;
    request.setDefault = true;
    CHECK(!Access(StudioSettings::IniBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request));
    CHECK(Load() == bad); // Bad values never become missing defaults or registry fallbacks.
    const std::string malformed = "[User]\ntruncated line\n";
    CHECK(SDL_SaveFile(StudioSettings::IniPath().c_str(), malformed.data(), malformed.size()));
    request.write = true;
    CHECK(!Access(StudioSettings::IniBackend(), key.c_str(), option.c_str(), &enabled, sizeof(enabled), request));
    CHECK(Load() == malformed);
#ifdef _WIN32
    HKEY parent;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\3DMMForever\\RetailOracle", 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY,
                      &parent) == ERROR_SUCCESS)
    {
        RegDeleteTreeA(parent, ("SDL-" + run).c_str());
        RegCloseKey(parent);
    }
#endif
    const auto directory = std::filesystem::u8path(StudioSettings::IniPath()).parent_path();
    CHECK(directory.filename() == run && directory.parent_path().filename() == "3dmmforever-settings");
    std::filesystem::remove_all(directory);
    std::puts("INI: migration, editable options, machine settings, exact blobs, strings, comments and malformed files "
              "passed.");
    return 0;
}
