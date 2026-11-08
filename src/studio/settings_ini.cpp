/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_settings.h"
#include "platform_files.h"
#include "platform_text.h"
#include "text_utf8.h"
#ifdef _WIN32
#include "settings_store_win.h"
#endif
#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
using namespace KauaiSettings;
namespace fs = std::filesystem;
std::string Trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
std::string Lower(std::string value)
{
    for (auto &ch : value)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    return value;
}
std::string Text(const NameChar *value)
{
#ifdef UNICODE
    std::vector<std::uint16_t> units(value, value + std::char_traits<NameChar>::length(value));
#elif defined(_WIN32)
    const auto length = KauaiText::ToUtf16(value, std::strlen(value), KauaiText::Encoding::WindowsAnsi, nullptr, 0);
    std::vector<std::uint16_t> units(length);
    if (length &&
        !KauaiText::ToUtf16(value, std::strlen(value), KauaiText::Encoding::WindowsAnsi, units.data(), length))
        throw std::runtime_error("Invalid settings text");
#else
    return value;
#endif
#if defined(UNICODE) || defined(_WIN32)
    std::string utf8;
    if (!KauaiText::EncodeUtf8(units, utf8))
        throw std::runtime_error("Invalid settings text");
    return utf8;
#endif
}
struct Document
{
    std::vector<std::string> lines;
    bool Parse(const std::string &input)
    {
        std::map<std::string, bool> keys;
        std::string section;
        for (std::size_t start = 0; start < input.size();)
        {
            const auto end = input.find('\n', start);
            auto line = input.substr(start, end == std::string::npos ? end : end - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (lines.empty() && line.compare(0, 3, "\xef\xbb\xbf") == 0)
                line.erase(0, 3);
            if (line.find('\0') != std::string::npos)
                return false;
            lines.push_back(line);
            line = Trim(line);
            if (!line.empty() && line[0] != ';' && line[0] != '#')
            {
                if (line.front() == '[' && line.back() == ']')
                    section = Lower(Trim(line.substr(1, line.size() - 2)));
                else
                {
                    const auto equals = line.find('=');
                    if (section.empty() || equals == std::string::npos || Trim(line.substr(0, equals)).empty() ||
                        !keys.emplace(section + '\n' + Lower(Trim(line.substr(0, equals))), true).second)
                        return false;
                }
            }
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        return true;
    }
    bool Get(const std::string &section, const std::string &key, std::string &value) const
    {
        std::string current;
        for (auto line : lines)
        {
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#')
                continue;
            if (line.front() == '[' && line.back() == ']')
                current = Lower(Trim(line.substr(1, line.size() - 2)));
            else if (current == Lower(section))
            {
                const auto equals = line.find('=');
                if (Lower(Trim(line.substr(0, equals))) == Lower(key))
                {
                    value = Trim(line.substr(equals + 1));
                    return true;
                }
            }
        }
        return false;
    }
    void Set(const std::string &section, const std::string &key, const std::string &value)
    {
        std::string current;
        std::size_t insert = lines.size();
        bool found = false;
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const auto line = Trim(lines[i]);
            if (!line.empty() && line.front() == '[' && line.back() == ']')
                current = Lower(Trim(line.substr(1, line.size() - 2)));
            if (current != Lower(section))
                continue;
            found = true;
            insert = i + 1;
            const auto equals = line.find('=');
            if (equals != std::string::npos && Lower(Trim(line.substr(0, equals))) == Lower(key))
            {
                lines[i] = key + '=' + value;
                return;
            }
        }
        if (!found)
        {
            lines.push_back("");
            lines.push_back('[' + section + ']');
            insert = lines.size();
        }
        lines.insert(lines.begin() + insert, key + '=' + value);
    }
    bool Save() const
    {
        std::string output;
        for (const auto &line : lines)
            output += line + '\n';
        const auto temporary = StudioSettings::IniPath() + ".tmp";
        return SDL_SaveFile(temporary.c_str(), output.data(), output.size()) &&
               SDL_RenamePath(temporary.c_str(), StudioSettings::IniPath().c_str());
    }
};
struct Store
{
    Document document;
    std::string section;
    KauaiFiles::FileHandle lock = nullptr;
    ~Store()
    {
        KauaiFiles::Close(lock);
    }
};
std::string Hex(const void *data, std::uint32_t bytes)
{
    const auto raw = static_cast<const unsigned char *>(data);
    std::string value;
    for (std::uint32_t i = 0; i < bytes; ++i)
    {
        value += "0123456789abcdef"[raw[i] >> 4];
        value += "0123456789abcdef"[raw[i] & 15];
    }
    return value;
}
bool Put(Document &document, const std::string &section, const NameChar *name, const void *data, std::uint32_t bytes,
         Type type)
{
    if ((!data && bytes) || bytes > 16 * 1024 * 1024)
        return false;
    const auto key = Text(name);
    if (key.empty() || key.find_first_of("=\r\n[];#") != std::string::npos)
        return false;
    std::string value, encoding;
    if ((type == Type::Number && bytes == 4) || (type == Type::Binary && (bytes == 1 || bytes == 4)))
    {
        std::uint32_t number = 0;
        std::memcpy(&number, data, bytes);
        value = std::to_string(number);
        encoding = type == Type::Number ? "number" : bytes == 1 ? "byte" : "int";
    }
    else if (type == Type::String && bytes >= sizeof(NameChar) && bytes % sizeof(NameChar) == 0)
    {
        const auto text = static_cast<const NameChar *>(data);
        if (text[bytes / sizeof(NameChar) - 1] != 0)
            return false;
        value = "\"";
        for (auto ch : Text(text))
        {
            if (ch == '\\' || ch == '"')
                value += '\\';
            if (ch == '\n' || ch == '\r')
                value += ch == '\n' ? "\\n" : "\\r";
            else
                value += ch;
        }
        value += '"';
        encoding = "string";
    }
    else if (type == Type::Binary)
    {
        value = Hex(data, bytes);
        encoding = "hex";
    }
    else
        return false;
    document.Set(section, key, value);
    document.Set(section + ".Types", key, encoding);
    return true;
}
Handle Open(const NameChar *key, bool machine, bool *created)
{
    try
    {
        const auto &path = StudioSettings::IniPath();
        if (path.empty())
            return nullptr;
        fs::create_directories(fs::u8path(path).parent_path());
        auto store = std::make_unique<Store>();
        const auto lockPath = fs::u8path(path + ".lock");
#ifdef UNICODE
        const auto units = lockPath.u16string();
        const std::basic_string<KauaiFiles::PathChar> native(units.begin(), units.end());
#else
        const auto native = lockPath.string();
#endif
        KauaiFiles::OpenOptions options;
        options.write = options.denyRead = true;
        options.creation = KauaiFiles::Creation::CreateAlways;
        for (int retry = 0; retry < 100 && !store->lock; ++retry)
        {
            store->lock = KauaiFiles::Open(native.c_str(), options);
            if (!store->lock)
                SDL_Delay(5);
        }
        if (!store->lock)
            return nullptr;
        store->section = machine ? "Machine" : "User";
        *created = !fs::exists(fs::u8path(path));
        if (*created)
        {
            store->document.lines.push_back(
                "; 3DMMForever settings. Numeric options are editable; .Types preserves legacy value sizes.");
#ifdef _WIN32
            for (const bool legacyMachine : {false, true})
            {
                std::vector<WindowsValue> values;
                if (!ReadWindowsValues(key, legacyMachine, values))
                    return nullptr;
                for (const auto &value : values)
                    if (!Put(store->document, legacyMachine ? "Machine" : "User", value.name.c_str(),
                             value.bytes.data(), static_cast<std::uint32_t>(value.bytes.size()), value.type))
                        return nullptr;
            }
#endif
            if (!store->document.Save())
                return nullptr;
            // Imported values must be read even for set-default operations.
            *created = false;
        }
        else
        {
            if (fs::file_size(fs::u8path(path)) > 40 * 1024 * 1024)
                return nullptr;
            std::size_t bytes = 0;
            std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile(path.c_str(), &bytes), SDL_free);
            if (!data || !store->document.Parse(std::string(static_cast<const char *>(data.get()), bytes)))
                return nullptr;
        }
        return store.release();
    }
    catch (...)
    {
        return nullptr;
    }
}
ReadResult Read(Handle handle, const NameChar *name, void *data, std::uint32_t *bytes, Type *type)
{
    try
    {
        const auto &store = *static_cast<Store *>(handle);
        std::string value, encoding;
        const auto key = Text(name);
        if (!store.document.Get(store.section, key, value))
            return ReadResult::Missing;
        if (!store.document.Get(store.section + ".Types", key, encoding))
            return ReadResult::Error;
        std::vector<unsigned char> payload;
        if (encoding == "number" || encoding == "byte" || encoding == "int")
        {
            std::uint32_t number = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
            if (result.ec != std::errc() || result.ptr != value.data() + value.size() ||
                (encoding == "byte" && number > 255))
                return ReadResult::Error;
            payload.resize(encoding == "byte" ? 1 : 4);
            std::memcpy(payload.data(), &number, payload.size());
            *type = encoding == "number" ? Type::Number : Type::Binary;
        }
        else if (encoding == "hex")
        {
            if (value.size() % 2)
                return ReadResult::Error;
            for (std::size_t i = 0; i < value.size(); i += 2)
            {
                unsigned number = 0;
                const auto result = std::from_chars(value.data() + i, value.data() + i + 2, number, 16);
                if (result.ec != std::errc() || result.ptr != value.data() + i + 2)
                    return ReadResult::Error;
                payload.push_back(static_cast<unsigned char>(number));
            }
            *type = Type::Binary;
        }
        else if (encoding == "string")
        {
            if (value.size() < 2 || value.front() != '"' || value.back() != '"')
                return ReadResult::Error;
            std::string text;
            for (std::size_t i = 1; i + 1 < value.size(); ++i)
            {
                char ch = value[i];
                if (ch == '\\')
                {
                    if (++i + 1 >= value.size())
                        return ReadResult::Error;
                    ch = value[i];
                    if (ch == 'n')
                        ch = '\n';
                    else if (ch == 'r')
                        ch = '\r';
                    else if (ch != '\\' && ch != '"')
                        return ReadResult::Error;
                }
                else if (ch == '"')
                    return ReadResult::Error;
                text += ch;
            }
            std::vector<std::uint16_t> units;
            if (!KauaiText::DecodeUtf8(text, units))
                return ReadResult::Error;
            units.push_back(0);
#ifdef UNICODE
            payload.resize(units.size() * 2);
            std::memcpy(payload.data(), units.data(), payload.size());
#elif defined(_WIN32)
            const auto count =
                KauaiText::ToAnsi(units.data(), units.size() * 2, KauaiText::Encoding::Utf16LE, nullptr, 0);
            payload.resize(count);
            if (!count || !KauaiText::ToAnsi(units.data(), units.size() * 2, KauaiText::Encoding::Utf16LE,
                                             reinterpret_cast<char *>(payload.data()), count))
                return ReadResult::Error;
#else
            payload.assign(text.begin(), text.end());
            payload.push_back(0);
#endif
            *type = Type::String;
        }
        else
            return ReadResult::Error;
        const auto capacity = *bytes;
        *bytes = static_cast<std::uint32_t>(payload.size());
        if (data && capacity < payload.size())
            return ReadResult::Error;
        if (data && !payload.empty())
            std::memcpy(data, payload.data(), payload.size());
        return ReadResult::Found;
    }
    catch (...)
    {
        return ReadResult::Error;
    }
}
bool Write(Handle handle, const NameChar *name, const void *data, std::uint32_t bytes, Type type)
{
    try
    {
        auto &store = *static_cast<Store *>(handle);
        return Put(store.document, store.section, name, data, bytes, type) && store.document.Save();
    }
    catch (...)
    {
        return false;
    }
}
void Close(Handle handle)
{
    delete static_cast<Store *>(handle);
}
} // namespace
const KauaiSettings::Backend &StudioSettings::IniBackend()
{
    static const KauaiSettings::Backend backend{Open, Read, Write, Close};
    return backend;
}
const std::string &StudioSettings::IniPath()
{
    static const std::string path = []() -> std::string {
        try
        {
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
            if (const auto run = std::getenv("KAUAI_TEST_SETTINGS_RUN"))
            {
                const std::string id(run);
                if (id.empty() || id.size() > 64 || id.find_first_not_of("0123456789abcdef-") != std::string::npos)
                    return {};
                return (fs::temp_directory_path() / "3dmmforever-settings" / id / "settings.ini").u8string();
            }
#endif
            std::unique_ptr<char, decltype(&SDL_free)> directory(SDL_GetPrefPath("3DMMForever", "Studio"), SDL_free);
            return directory ? std::string(directory.get()) + "settings.ini" : std::string();
        }
        catch (...)
        {
            return {};
        }
    }();
    return path;
}
