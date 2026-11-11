/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "protocol.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <intrin.h>
using namespace RetailOracle;
namespace
{
HMODULE module;
Config config;
std::uintptr_t base;
HHOOK hook;
HANDLE done;
CRITICAL_SECTION filesLock;
std::set<std::string> openedAssets;
std::set<std::string> fileLookups;
std::atomic<bool> pending{false}, poisoned{false};
Request request;
std::string response;
std::atomic<unsigned> registryRedirects{0};
char rootPath[520];
char homePath[600];
HKEY privateUserRoot;
void *movieCache = nullptr, *executorCache = nullptr;
void *errorCache = nullptr;
using RegQuery = LSTATUS(WINAPI *)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
using OpenFileFunction = HANDLE(WINAPI *)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateKeyFunction = LSTATUS(WINAPI *)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES, PHKEY,
                                            LPDWORD);
using FindFileFunction = HANDLE(WINAPI *)(LPCSTR, LPWIN32_FIND_DATAA);
using SemaphoreFunction = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR);
SemaphoreFunction realSemaphore;
HANDLE WINAPI CreatePrivateSemaphore(LPSECURITY_ATTRIBUTES security, LONG initial, LONG maximum, LPCSTR name)
{
    if (!name)
        return realSemaphore(security, initial, maximum, nullptr);
    HANDLE handle;
    DWORD error;
    {
        auto isolated = "3DMMForever-Oracle-" + std::to_string(GetCurrentProcessId()) + '-' + name;
        handle = realSemaphore(security, initial, maximum, isolated.c_str());
        error = GetLastError();
    }
    SetLastError(error);
    return handle;
}
RegQuery realRegQuery;
OpenFileFunction realOpenFile;
CreateKeyFunction realCreateKey;
CreateKeyFunction shimCreateKey;
FindFileFunction realFindFile;
struct FrameObservation
{
    int scene, frame, first, last;
    std::uint64_t nanoseconds;
};
constexpr DWORD FrameCommitReturnRva = 0x000a1131;
struct CallbackObservation
{
    int scene, frame;
    DWORD caller;
    std::uint64_t nanoseconds;
};
std::vector<CallbackObservation> callbackTrace;
std::vector<FrameObservation> frameTrace;
bool traceOverflow = false;
DWORD *traceSlot = nullptr;
using TimelineUpdate = void(__thiscall *)(void *);
TimelineUpdate realTimelineUpdate;
struct SoundObservation
{
    DWORD type, number, start;
    int queue, volume, plays, priority, category, scene, frame;
};
std::vector<SoundObservation> soundTrace;
DWORD *soundSlot = nullptr;
void *soundCache = nullptr;
using PlaySound = int(__thiscall *)(void *, void *, DWORD, DWORD, int, int, int, DWORD, int, int);
PlaySound realPlaySound;
std::string Quote(const std::string &text)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : text)
        if (ch == '"' || ch == '\\')
            out << '\\' << ch;
        else if (ch < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(ch) << std::dec;
        else
            out << ch;
    out << '"';
    return out.str();
}
std::string Utf8(const wchar_t *path)
{
    const auto count = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    std::string result(count, 0);
    if (count)
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &result[0], count, nullptr, nullptr);
    if (!result.empty())
        result.pop_back();
    return result;
}
std::string AnsiUtf8(const std::string &text)
{
    const auto count = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    if (!count)
        throw std::runtime_error("invalid_ansi_path");
    std::wstring wide(count, 0);
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, &wide[0], count);
    return Utf8(wide.c_str());
}
bool Readable(const void *pointer, std::size_t bytes)
{
    MEMORY_BASIC_INFORMATION memory;
    if (!pointer || !VirtualQuery(pointer, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    return reinterpret_cast<std::uintptr_t>(pointer) + bytes >= reinterpret_cast<std::uintptr_t>(pointer) &&
           reinterpret_cast<std::uintptr_t>(pointer) + bytes <=
               reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
}
template <class T> T Read(const void *object, std::size_t offset = 0)
{
    const auto pointer = static_cast<const BYTE *>(object) + offset;
    if (!Readable(pointer, sizeof(T)))
        throw std::runtime_error("invalid_native_object");
    return *reinterpret_cast<const T *>(pointer);
}
void *Pointer(const void *object, std::size_t offset)
{
    return reinterpret_cast<void *>(Read<DWORD>(object, offset));
}
bool Instance(const void *object, DWORD vtable)
{
    return Readable(object, 8) && *static_cast<const DWORD *>(object) == base + vtable;
}
void *FindObject(DWORD vtable, void *&cached)
{
    if (Instance(cached, vtable))
        return cached;
    std::vector<void *> matches;
    std::uintptr_t address = 0x10000;
    MEMORY_BASIC_INFORMATION memory;
    while (address < 0x7fff0000 && VirtualQuery(reinterpret_cast<void *>(address), &memory, sizeof(memory)))
    {
        const auto end = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        if (end <= address)
            break;
        const auto ownImage =
            memory.Type == MEM_IMAGE && reinterpret_cast<std::uintptr_t>(memory.AllocationBase) == base;
        if (memory.State == MEM_COMMIT && (memory.Type == MEM_PRIVATE || ownImage) &&
            (memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) && !(memory.Protect & PAGE_GUARD))
        {
            const auto words = static_cast<const DWORD *>(memory.BaseAddress);
            for (std::size_t i = 0; i < memory.RegionSize / sizeof(DWORD); ++i)
                if (words[i] == base + vtable)
                    matches.push_back(const_cast<DWORD *>(words + i));
        }
        address = end;
    }
    if (matches.size() != 1)
        throw std::runtime_error(matches.empty() ? "native_object_not_ready" : "ambiguous_native_object");
    cached = matches.front();
    return cached;
}
void *Movie()
{
    auto movie = FindObject(MovieVtable, movieCache);
    const auto count = Read<int>(movie, 0x14c), scene = Read<int>(movie, 0x150);
    if (count < 0 || count > 100000 || scene < -1 || scene >= count)
        throw std::runtime_error("invalid_movie_layout");
    return movie;
}
void *Scene(void *movie)
{
    void *scene = Pointer(movie, 0x148);
    if (!Instance(scene, SceneVtable))
        throw std::runtime_error("scene_not_ready");
    const auto first = Read<int>(scene, 0x10), last = Read<int>(scene, 0xc);
    if (last < first)
        throw std::runtime_error("invalid_scene_layout");
    return scene;
}
int Count(void *group)
{
    if (!group)
        return 0;
    const auto count = Read<int>(group, 0x20);
    if (count < 0 || count > 100000)
        throw std::runtime_error("invalid_group_layout");
    return count;
}
void Actors(std::ostringstream &out, void *scene)
{
    void *group = Pointer(scene, 0x120);
    const auto count = Count(group);
    const auto bytes = count ? Read<int>(group, 0x24) : 4;
    if (bytes != 4)
        throw std::runtime_error("invalid_actor_list_stride");
    void *data = count ? Pointer(group, 0x10) : nullptr;
    out << '[';
    for (int index = 0; index < count; ++index)
    {
        void *actor = Pointer(data, index * 4);
        if (!Instance(actor, ActorVtable))
            throw std::runtime_error("invalid_actor_layout");
        if (index)
            out << ',';
        out << "{\"id\":" << Read<int>(actor, 0x4c) << ",\"action\":" << Read<int>(actor, 0x74)
            << ",\"cel\":" << Read<int>(actor, 0x7c) << ",\"frame\":" << Read<int>(actor, 0x80)
            << ",\"next_event\":" << Read<int>(actor, 0x88)
            << ",\"on_stage\":" << ((Read<DWORD>(actor, 0x6c) & 1) ? "true" : "false")
            << ",\"template_sid\":" << Read<int>(actor, 0x18) << ",\"template_cno\":" << Read<DWORD>(actor, 0x24)
            << ",\"position\":[";
        for (int axis = 0; axis < 3; ++axis)
        {
            if (axis)
                out << ',';
            // This EXE uses BRender's signed 16.16 scalar build.
            out << std::setprecision(12) << Read<std::int32_t>(actor, 0xa4 + axis * 4) / 65536.0;
        }
        out << "],\"root_matrix_fixed\":[";
        // Hash-pinned x86 ACTR::_pbody and BODY::_prgbact layouts.
        const auto body = Pointer(actor, 0x14);
        const auto root = Pointer(body, 8);
        if (Read<std::uint16_t>(root, 36) > 1)
            throw std::runtime_error("unsupported_root_transform");
        for (int component = 0; component < 12; ++component)
        {
            if (component) out << ',';
            out << Read<std::int32_t>(root, 40 + component * 4);
        }
        out << "],\"part_matrices_fixed\":[";
        // Original BACT is 92 bytes; its matrix starts at byte 40.
        // Body slots 0 and 1 are the root and selection highlight.
        const int parts = Read<int>(body, 24);
        if (parts < 0 || parts > 2048)
            throw std::runtime_error("invalid_body_part_count");
        for (int part = 0; part < parts; ++part)
            for (int component = 0; component < 12; ++component)
            {
                if (part || component) out << ',';
                out << Read<std::int32_t>(root, (part + 2) * 92 + 40 + component * 4);
            }
        out << "]}";
    }
    out << ']';
}
std::string StateJSON()
{
    void *movie = Movie(), *scene = Scene(movie);
    std::ostringstream out;
    const int first = Read<int>(scene, 0x10), frame = Read<int>(scene, 8);
    void *errors = FindObject(ErrorVtable, errorCache);
    using ErrorCount = int(__thiscall *)(void *);
    const auto countErrors = reinterpret_cast<ErrorCount>(Read<DWORD>(Pointer(errors, 0), 8 * 4))(errors);
    void *background = Pointer(scene, 0x130);
    auto sound = FindObject(SoundManagerVtable, soundCache);
    using Playing = int(__thiscall *)(void *, int, int);
    auto playing = reinterpret_cast<Playing>(Read<DWORD>(Pointer(sound, 0), 18 * 4));
    bool soundPlaying = playing(sound, -1, 0) || playing(sound, -1, 1) || playing(sound, -1, 2);
    out << "{\"backend\":\"" << (config.profile ? "v3dmm-014-us" : "retail-iso")
        << "\",\"ui_thread\":" << GetCurrentThreadId() << ",\"movie_scene_count\":" << Read<int>(movie, 0x14c)
        << ",\"movie_scene\":" << Read<int>(movie, 0x150) << ",\"scene_number\":" << Read<int>(movie, 0x150) + 1
        << ",\"scene_frame\":" << frame << ",\"scene_first_frame\":" << first
        << ",\"scene_last_frame\":" << Read<int>(scene, 0xc) << ",\"frame_number\":" << frame - first + 1
        << ",\"movie_playing\":" << ((Read<DWORD>(movie, 0x154) & 4) ? "true" : "false")
        << ",\"error_stack_depth\":" << countErrors << ",\"movie_sound_playing\":" << (soundPlaying ? "true" : "false")
        << ",\"scene_camera\":" << (background ? Read<int>(background, 0x30) : -1)
        << ",\"scene_camera_count\":" << (background ? Read<int>(background, 0x2c) : 0)
        << ",\"scene_actor_count\":" << Count(Pointer(scene, 0x120))
        << ",\"scene_textbox_count\":" << Count(Pointer(scene, 0x124)) << ",\"scene_actors\":";
    Actors(out, scene);
    out << '}';
    return out.str();
}
LSTATUS WINAPI QueryRegistry(HKEY key, LPCSTR value, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD bytes)
{
    if (value && (!_stricmp(value, "InstallDirectory") || !_stricmp(value, "HomeDirectory")))
    {
        if (config.profile)
        {
            ++registryRedirects;
            return realRegQuery(key, value, reserved, type, data, bytes);
        }
        if (!bytes)
            return ERROR_INVALID_PARAMETER;
        const char *text = !_stricmp(value, "HomeDirectory") ? homePath : rootPath;
        const DWORD needed = static_cast<DWORD>(strlen(text) + 1);
        if (type)
            *type = REG_SZ;
        if (!data)
        {
            *bytes = needed;
            return ERROR_SUCCESS;
        }
        if (*bytes < needed)
        {
            *bytes = needed;
            return ERROR_MORE_DATA;
        }
        memcpy(data, text, needed);
        *bytes = needed;
        ++registryRedirects;
        return ERROR_SUCCESS;
    }
    return realRegQuery(key, value, reserved, type, data, bytes);
}
LSTATUS WINAPI CreateUserKey(HKEY key, LPCSTR subkey, DWORD reserved, LPSTR cls, DWORD options, REGSAM access,
                             LPSECURITY_ATTRIBUTES security, PHKEY result, LPDWORD disposition)
{
    return (shimCreateKey ? shimCreateKey : realCreateKey)(key == HKEY_CURRENT_USER ? privateUserRoot : key, subkey,
                                                           reserved, cls, options, access, security, result,
                                                           disposition);
}
HANDLE WINAPI FindFileTracked(LPCSTR path, LPWIN32_FIND_DATAA data)
{
    HANDLE result = realFindFile(path, data);
    const auto error = GetLastError();
    if (path)
    {
        EnterCriticalSection(&filesLock);
        if (fileLookups.size() < 1000)
            fileLookups.insert(std::string(result == INVALID_HANDLE_VALUE ? "missing:" : "found:") + path);
        LeaveCriticalSection(&filesLock);
    }
    SetLastError(error);
    return result;
}
HANDLE WINAPI CreateFileTracked(LPCSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
                                DWORD disposition, DWORD attributes, HANDLE templateFile)
{
    HANDLE file = realOpenFile(path, access, sharing, security, disposition, attributes, templateFile);
    const DWORD error = GetLastError();
    if (file != INVALID_HANDLE_VALUE && path)
    {
        std::string name(path), lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char ch) { return char(std::tolower(ch)); });
        const auto extension = lower.find_last_of('.');
        if (extension != std::string::npos && (lower.substr(extension) == ".chk" || lower.substr(extension) == ".3cn" ||
                                               lower.substr(extension) == ".3th"))
        {
            EnterCriticalSection(&filesLock);
            openedAssets.insert(name);
            LeaveCriticalSection(&filesLock);
        }
    }
    SetLastError(error);
    return file;
}
bool PatchImport(void *original, void *replacement)
{
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    const auto pe = reinterpret_cast<IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(
        base + pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (auto entry = imports; entry->Name; ++entry)
        for (auto thunk = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + entry->FirstThunk); thunk->u1.Function; ++thunk)
            if (thunk->u1.Function == reinterpret_cast<DWORD>(original))
            {
                DWORD protection;
                if (!VirtualProtect(&thunk->u1.Function, 4, PAGE_READWRITE, &protection))
                    return false;
                InterlockedExchange(reinterpret_cast<LONG *>(&thunk->u1.Function), reinterpret_cast<LONG>(replacement));
                VirtualProtect(&thunk->u1.Function, 4, protection, &protection);
                return true;
            }
    return false;
}
HWND MainWindow()
{
    HWND result = nullptr;
    EnumThreadWindows(
        config.uiThread,
        [](HWND window, LPARAM argument) -> BOOL {
            wchar_t cls[80];
            GetClassNameW(window, cls, 80);
            RECT rect;
            GetClientRect(window, &rect);
            if (!wcscmp(cls, L"3DMOVIE") && rect.right >= 640 && rect.bottom >= 480)
            {
                *reinterpret_cast<HWND *>(argument) = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    if (!result)
        throw std::runtime_error("main_window_not_ready");
    return result;
}
void CaptureBMP(const wchar_t *path)
{
    HWND window = MainWindow();
    RECT bounds;
    GetClientRect(window, &bounds);
    HDC screen = GetDC(window), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, bounds.right, bounds.bottom);
    HGDIOBJ old = SelectObject(memory, bitmap);
    const auto painted = PrintWindow(window, memory, PW_CLIENTONLY);
    SelectObject(memory, old);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.right;
    info.bmiHeader.biHeight = -bounds.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> pixels(bounds.right * bounds.bottom * 4);
    const auto rows = GetDIBits(memory, bitmap, 0, bounds.bottom, pixels.data(), &info, DIB_RGB_COLORS);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, screen);
    if (!painted || rows != bounds.bottom)
        throw std::runtime_error("capture_failed");
    BITMAPFILEHEADER header = {};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    file.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    file.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
    if (!file)
        throw std::runtime_error("cannot_write_capture");
}
void Refresh(void *movie, bool render)
{
    using Action = void(__thiscall *)(void *);
    if (render)
        reinterpret_cast<Action>(base + RenderWorld)(Pointer(movie, 0x158));
    // MVIE::FCmdRender uses this same MCC callback to update the timeline.
    void *mcc = Pointer(movie, 0x194);
    auto table = Pointer(mcc, 0);
    reinterpret_cast<Action>(Read<DWORD>(table, 0x2c))(mcc);
    InvalidateRect(MainWindow(), nullptr, FALSE);
    UpdateWindow(MainWindow());
}
void __fastcall ObserveTimeline(void *mcc, void *)
{
    // The hash-pinned EXEs also refresh this callback from Play and scene
    // preload. Only FCmdRender's DisplayFrame call is a presentation commit.
    // Its return RVA was verified against both original runtime profiles.
    const auto caller = DWORD(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base);
    try
    {
        auto movie = Movie();
        auto scene = Scene(movie);
        const int index = Read<int>(movie, 0x150), frame = Read<int>(scene, 8);
        if (Read<DWORD>(movie, 0x154) & 4)
        {
            LARGE_INTEGER now, frequency;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&frequency);
            if (callbackTrace.size() == 100000)
                traceOverflow = true;
            else
                callbackTrace.push_back(
                    {index, frame, caller,
                     std::uint64_t(now.QuadPart / frequency.QuadPart) * 1000000000 +
                         std::uint64_t(now.QuadPart % frequency.QuadPart) * 1000000000 / frequency.QuadPart});
        }
        if (caller == FrameCommitReturnRva && (Read<DWORD>(movie, 0x154) & 4) &&
            (frameTrace.empty() || frameTrace.back().scene != index || frameTrace.back().frame != frame))
        {
            LARGE_INTEGER now, frequency;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&frequency);
            if (frameTrace.size() == 100000)
                traceOverflow = true;
            else
                frameTrace.push_back(
                    {index, frame, Read<int>(scene, 0x10), Read<int>(scene, 0xc),
                     std::uint64_t(now.QuadPart / frequency.QuadPart) * 1000000000 +
                         std::uint64_t(now.QuadPart % frequency.QuadPart) * 1000000000 / frequency.QuadPart});
        }
    }
    catch (...)
    {
        traceOverflow = true;
    }
    realTimelineUpdate(mcc);
}
int __fastcall ObserveSound(void *device, void *, void *resource, DWORD type, DWORD number, int queue, int volume,
                            int plays, DWORD start, int priority, int category)
{
    try
    {
        auto movie = Movie();
        auto scene = Scene(movie);
        if (soundTrace.size() == 10000)
            traceOverflow = true;
        else
            soundTrace.push_back({type, number, start, queue, volume, plays, priority, category,
                                  Read<int>(movie, 0x150), Read<int>(scene, 8)});
    }
    catch (...)
    {
        traceOverflow = true;
    }
    return realPlaySound(device, resource, type, number, queue, volume, plays, start, priority, category);
}
void TraceHook(bool enable)
{
    if (enable && traceSlot)
        throw std::runtime_error("trace_already_active");
    if (enable)
    {
        auto sound = FindObject(SoundManagerVtable, soundCache);
        using StopAll = void(__thiscall *)(void *, int, int);
        reinterpret_cast<StopAll>(Read<DWORD>(Pointer(sound, 0), 12 * 4))(sound, -1, -1);
        auto mcc = Pointer(Movie(), 0x194);
        traceSlot = static_cast<DWORD *>(Pointer(mcc, 0)) + 11;
        realTimelineUpdate = reinterpret_cast<TimelineUpdate>(*traceSlot);
        frameTrace.clear();
        frameTrace.reserve(100000);
        callbackTrace.clear();
        callbackTrace.reserve(100000);
        traceOverflow = false;
        soundSlot = static_cast<DWORD *>(Pointer(sound, 0)) + 10;
        realPlaySound = reinterpret_cast<PlaySound>(*soundSlot);
        soundTrace.clear();
        soundTrace.reserve(10000);
    }
    if (traceSlot)
    {
        DWORD protection;
        if (!VirtualProtect(traceSlot, 4, PAGE_READWRITE, &protection))
            throw std::runtime_error("cannot_set_trace_observer");
        *traceSlot = enable ? reinterpret_cast<DWORD>(&ObserveTimeline) : reinterpret_cast<DWORD>(realTimelineUpdate);
        VirtualProtect(traceSlot, 4, protection, &protection);
        if (!enable)
            traceSlot = nullptr;
    }
    if (soundSlot)
    {
        DWORD protection;
        if (!VirtualProtect(soundSlot, 4, PAGE_READWRITE, &protection))
            throw std::runtime_error("cannot_set_sound_observer");
        *soundSlot = enable ? reinterpret_cast<DWORD>(&ObserveSound) : reinterpret_cast<DWORD>(realPlaySound);
        VirtualProtect(soundSlot, 4, protection, &protection);
        if (!enable)
            soundSlot = nullptr;
    }
}
std::string Process(const Request &job)
{
    if (job.operation == TraceBegin)
    {
        if (Read<DWORD>(Movie(), 0x154) & 4)
            throw std::runtime_error("cannot_arm_trace_during_playback");
        TraceHook(true);
        return "{\"armed\":true}";
    }
    if (job.operation == TraceEnd)
    {
        TraceHook(false);
        std::ostringstream out;
        out << "{\"trace_version\":2,\"frame_commit_caller_rva\":" << FrameCommitReturnRva
            << ",\"overflow\":" << (traceOverflow ? "true" : "false") << ",\"frames\":[";
        for (unsigned i = 0; i < frameTrace.size(); ++i)
        {
            if (i)
                out << ',';
            const auto &frame = frameTrace[i];
            out << "{\"scene\":" << frame.scene << ",\"frame\":" << frame.frame << ",\"first\":" << frame.first
                << ",\"last\":" << frame.last << ",\"monotonic_ns\":" << frame.nanoseconds << '}';
        }
        out << "],\"callbacks\":[";
        for (unsigned i = 0; i < callbackTrace.size(); ++i)
        {
            if (i)
                out << ',';
            const auto &callback = callbackTrace[i];
            out << "{\"scene\":" << callback.scene << ",\"frame\":" << callback.frame
                << ",\"caller_rva\":" << callback.caller << ",\"monotonic_ns\":" << callback.nanoseconds << '}';
        }
        out << "],\"sounds\":[";
        for (unsigned i = 0; i < soundTrace.size(); ++i)
        {
            if (i)
                out << ',';
            const auto &sound = soundTrace[i];
            out << "{\"type\":" << sound.type << ",\"number\":" << sound.number << ",\"queue\":" << sound.queue
                << ",\"volume\":" << sound.volume << ",\"plays\":" << sound.plays << ",\"start\":" << sound.start
                << ",\"priority\":" << sound.priority << ",\"category\":" << sound.category
                << ",\"scene\":" << sound.scene << ",\"frame\":" << sound.frame << '}';
        }
        out << "]}";
        return out.str();
    }
    if (job.operation == State)
        return StateJSON();
    if (job.operation == Capture)
    {
        CaptureBMP(job.path);
        return "{\"captured\":true,\"path\":" + Quote(Utf8(job.path)) + "}";
    }
    if (job.operation == Command)
    {
        void *executor = FindObject(ExecutorVtable, executorCache);
        auto table = Pointer(executor, 0);
        using Enqueue = void(__thiscall *)(void *, int, void *, void *, int, int, int, int);
        reinterpret_cast<Enqueue>(Read<DWORD>(table, 17 * 4))(executor, job.arguments[0], nullptr, nullptr,
                                                              job.arguments[1], job.arguments[2], job.arguments[3],
                                                              job.arguments[4]);
        return "{\"queued\":true}";
    }
    if (job.operation == Seek)
    {
        void *movie = Movie();
        if (Read<DWORD>(movie, 0x154) & 4)
            throw std::runtime_error("cannot_seek_during_playback");
        if (job.arguments[0] < 1 || job.arguments[0] > Read<int>(movie, 0x14c))
            throw std::runtime_error("scene_out_of_range");
        const int sceneIndex = job.arguments[0] - 1;
        using Move = int(__thiscall *)(void *, int);
        bool success = reinterpret_cast<Move>(base + SwitchScene)(movie, sceneIndex) != 0;
        void *scene = Scene(movie);
        const int first = Read<int>(scene, 0x10), last = Read<int>(scene, 0xc);
        const auto frame = std::int64_t(first) + job.arguments[1] - 1;
        if (job.arguments[1] < 1 || frame > last)
            throw std::runtime_error("frame_out_of_range");
        if (success)
            success = reinterpret_cast<Move>(base + GotoFrame)(scene, static_cast<int>(frame)) != 0;
        Refresh(movie, success);
        return std::string("{\"success\":") + (success ? "true" : "false") + ",\"state\":" + StateJSON() + "}";
    }
    throw std::runtime_error("unsupported_operation");
}
void ExecuteSafe()
{
    try
    {
        response = "{\"ok\":true,\"result\":" + Process(request) + "}";
    }
    catch (const std::exception &error)
    {
        response = "{\"ok\":false,\"error\":" + Quote(error.what()) + "}";
    }
}
void ExecuteGuarded()
{
    __try
    {
        ExecuteSafe();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        poisoned.store(true);
        response = "{\"ok\":false,\"error\":\"native_exception\"}";
    }
}
LRESULT CALLBACK MessageHook(int code, WPARAM removed, LPARAM argument)
{
    if (code >= 0 && removed == PM_REMOVE)
    {
        auto message = reinterpret_cast<MSG *>(argument);
        if (message->message == JobMessage && message->wParam == Magic && pending.load())
        {
            message->message = WM_NULL;
            ExecuteGuarded();
            SetEvent(done);
        }
    }
    return CallNextHookEx(hook, code, removed, argument);
}
bool Transfer(HANDLE pipe, void *data, DWORD bytes, bool writing)
{
    DWORD completed = 0;
    while (completed < bytes)
    {
        DWORD count = 0;
        const bool ok =
            writing ? WriteFile(pipe, static_cast<BYTE *>(data) + completed, bytes - completed, &count, nullptr) != 0
                    : ReadFile(pipe, static_cast<BYTE *>(data) + completed, bytes - completed, &count, nullptr) != 0;
        if (!ok || !count)
            return false;
        completed += count;
    }
    return true;
}
std::string HelloJSON()
{
    std::ostringstream out;
    out << "{\"ok\":true,\"result\":{\"protocol\":1,\"pid\":" << GetCurrentProcessId() << ",\"exe_sha256\":\""
        << (config.profile ? V3dmmSHA256 : ExeSHA256) << "\",\"backend\":\""
        << (config.profile ? "v3dmm-014-us" : "retail-iso") << "\",\"content_root\":" << Quote(AnsiUtf8(rootPath))
        << ",\"render_policy\":\"windowed, better quality, textures, no wireframe\""
        << ",\"registry_redirects\":" << registryRedirects.load() << ",\"opened_assets\":[";
    EnterCriticalSection(&filesLock);
    bool first = true;
    for (const auto &path : openedAssets)
    {
        if (!first)
            out << ',';
        first = false;
        out << Quote(AnsiUtf8(path));
    }
    LeaveCriticalSection(&filesLock);
    out << "],\"file_lookups\":[";
    EnterCriticalSection(&filesLock);
    first = true;
    for (const auto &path : fileLookups)
    {
        if (!first)
            out << ',';
        first = false;
        out << Quote(AnsiUtf8(path));
    }
    LeaveCriticalSection(&filesLock);
    out << "],\"search_paths\":[";
    if (config.profile)
    {
        using Paths = char **(__cdecl *)();
        using Free = void(__cdecl *)(void *);
        const auto physics = GetModuleHandleW(L"physfs.dll");
        auto paths = reinterpret_cast<Paths>(GetProcAddress(physics, "PHYSFS_getSearchPath"))();
        for (unsigned i = 0; paths && paths[i]; ++i)
        {
            if (i)
                out << ',';
            out << Quote(AnsiUtf8(paths[i]));
        }
        reinterpret_cast<Free>(GetProcAddress(physics, "PHYSFS_freeList"))(paths);
    }
    out << "],\"runtime_modules\":{";
    if (config.profile)
    {
        bool first = true;
        for (const wchar_t *name :
             {L"VFS.dll", L"physfs.dll", L"msvcr71.dll", L"zlib1.dll", L"libpng13.dll", L"libcurl.dll"})
        {
            wchar_t path[MAX_PATH]{};
            auto loaded = GetModuleHandleW(name);
            if (loaded)
                GetModuleFileNameW(loaded, path, MAX_PATH);
            if (!first)
                out << ',';
            first = false;
            out << Quote(Utf8(name)) << ':' << (loaded ? Quote(Utf8(path)) : "null");
        }
    }
    out << "}}}";
    return out.str();
}
DWORD WINAPI PipeThread(void *)
{
    hook = SetWindowsHookExW(WH_GETMESSAGE, MessageHook, module, config.uiThread);
    if (!hook)
        return GetLastError();
    wchar_t name[128];
    swprintf_s(name, L"\\\\.\\pipe\\3dmm-retail-oracle-%lu", GetCurrentProcessId());
    for (;;)
    {
        HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                       65536, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            return GetLastError();
        Request incoming;
        if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED)
            while (Transfer(pipe, &incoming, sizeof(incoming), false))
            {
                incoming.path[511] = 0;
                if (incoming.magic != Magic || incoming.version != Version)
                    break;
                std::string reply;
                if (poisoned.load())
                    reply = "{\"ok\":false,\"error\":\"bridge_poisoned\"}";
                else if (incoming.operation == Hello)
                    reply = HelloJSON();
                else
                {
                    request = incoming;
                    ResetEvent(done);
                    pending.store(true);
                    if (!PostThreadMessageW(config.uiThread, JobMessage, Magic, 0) ||
                        WaitForSingleObject(done, 10000) != WAIT_OBJECT_0)
                    {
                        // A late UI callback can still finish. Leave its request
                        // and response alone, and reject every future operation.
                        poisoned.store(true);
                        reply = "{\"ok\":false,\"error\":\"ui_timeout\"}";
                    }
                    else
                        reply = response;
                    pending.store(false);
                }
                DWORD bytes = static_cast<DWORD>(reply.size());
                if (!Transfer(pipe, &bytes, sizeof(bytes), true) || !Transfer(pipe, &reply[0], bytes, true))
                    break;
            }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI OracleStart(void *argument)
{
    config = *static_cast<Config *>(argument);
    base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    const auto pe = reinterpret_cast<IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    if (config.magic != Magic || config.version != ConfigVersion || config.profile > 1 ||
        pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || pe->FileHeader.TimeDateStamp != ExeTimestamp ||
        *reinterpret_cast<BYTE *>(base + GotoFrame) != 0x83)
        return 11;
    InitializeCriticalSection(&filesLock);
    BOOL substituted = FALSE;
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, config.contentRoot, -1, rootPath, sizeof(rootPath), nullptr,
                             &substituted) ||
        substituted)
        return 12;
    realRegQuery = reinterpret_cast<RegQuery>(GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegQueryValueExA"));
    realOpenFile = reinterpret_cast<OpenFileFunction>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateFileA"));
    realCreateKey =
        reinterpret_cast<CreateKeyFunction>(GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "RegCreateKeyExA"));
    realFindFile =
        reinterpret_cast<FindFileFunction>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FindFirstFileA"));
    realSemaphore =
        reinterpret_cast<SemaphoreFunction>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateSemaphoreA"));
    if (config.profile)
    {
        const auto vfs = GetModuleHandleW(L"VFS.dll");
        if (!vfs || !GetModuleHandleW(L"physfs.dll"))
            return 19;
        realRegQuery = reinterpret_cast<RegQuery>(GetProcAddress(vfs, "VegQueryValueExA"));
        realOpenFile = reinterpret_cast<OpenFileFunction>(GetProcAddress(vfs, "ZreateFileA"));
        realFindFile = reinterpret_cast<FindFileFunction>(GetProcAddress(vfs, "ZindFirstFileA"));
        shimCreateKey = reinterpret_cast<CreateKeyFunction>(GetProcAddress(vfs, "VegCreateKeyExA"));
        realSemaphore = reinterpret_cast<SemaphoreFunction>(GetProcAddress(vfs, "ZreateSemaphoreA"));
        if (!realRegQuery || !realOpenFile || !realFindFile || !shimCreateKey)
            return 20;
    }
    char privateKey[160];
    sprintf_s(privateKey, "Software\\3DMMForever\\RetailOracle\\Run-%lu", GetCurrentProcessId());
    if (realCreateKey(HKEY_CURRENT_USER, privateKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &privateUserRoot,
                      nullptr) != ERROR_SUCCESS)
        return 16;
    // Seed only this application's preferences, then redirect its HKCU writes.
    HKEY original = nullptr, destination = nullptr;
    const char appKey[] = "Software\\Microsoft\\Microsoft Kids\\3D Movie Maker";
    if (realCreateKey(privateUserRoot, appKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &destination, nullptr) !=
        ERROR_SUCCESS)
        return 17;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, appKey, 0, KEY_READ, &original) == ERROR_SUCCESS)
    {
        RegCopyTreeA(original, nullptr, destination);
        RegCloseKey(original);
    }
    // Pin display policy inside this private branch for repeatable captures.
    const DWORD disabled = 0, enabled = 1;
    for (const char *name : {"BetterSpeed", "SwitchResolution", "Wireframe", "NoTexture", "ShowPoseReadout"})
        if (RegSetValueExA(destination, name, 0, REG_BINARY, reinterpret_cast<const BYTE *>(&disabled), 4) !=
            ERROR_SUCCESS)
            return 18;
    RegSetValueExA(destination, "SkipSplashScreen", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&enabled), 4);
    RegSetValueExA(destination, "GreaterThan8bppMsg", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&disabled), 4);
    RegCloseKey(destination);
    sprintf_s(homePath, "%s\\Users\\Oracle", rootPath);
    CreateDirectoryA(homePath, nullptr);
    if (!PatchImport(reinterpret_cast<void *>(realRegQuery), reinterpret_cast<void *>(&QueryRegistry)) ||
        !PatchImport(reinterpret_cast<void *>(realOpenFile), reinterpret_cast<void *>(&CreateFileTracked)) ||
        !PatchImport(reinterpret_cast<void *>(shimCreateKey ? shimCreateKey : realCreateKey),
                     reinterpret_cast<void *>(&CreateUserKey)) ||
        !PatchImport(reinterpret_cast<void *>(realFindFile), reinterpret_cast<void *>(&FindFileTracked)) ||
        !PatchImport(reinterpret_cast<void *>(realSemaphore), reinterpret_cast<void *>(&CreatePrivateSemaphore)))
        return 13;
    done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!done)
        return 14;
    HANDLE worker = CreateThread(nullptr, 0, PipeThread, nullptr, 0, nullptr);
    if (!worker)
        return 15;
    CloseHandle(worker);
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
