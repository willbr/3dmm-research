/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "protocol.h"
#include <tlhelp32.h>
#include <wincrypt.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace RetailOracle;
static void Check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(std::string(message) + " (Windows error " + std::to_string(GetLastError()) + ")");
}
static std::string Hash(const wchar_t *path)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    Check(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != 0,
          "Acquire hash provider");
    Check(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != 0, "Create SHA256");
    std::ifstream file(path, std::ios::binary);
    Check(file.good(), "Open retail EXE");
    char block[65536];
    while (file.read(block, sizeof(block)) || file.gcount())
        Check(CryptHashData(hash, reinterpret_cast<const BYTE *>(block), static_cast<DWORD>(file.gcount()), 0) != 0,
              "Hash retail EXE");
    BYTE bytes[32];
    DWORD count = sizeof(bytes);
    Check(CryptGetHashParam(hash, HP_HASHVAL, bytes, &count, 0) != 0, "Read SHA256");
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    const char hex[] = "0123456789abcdef";
    std::string result;
    for (BYTE value : bytes)
    {
        result += hex[value >> 4];
        result += hex[value & 15];
    }
    return result;
}
static std::uintptr_t RemoteModule(DWORD pid, const wchar_t *name)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    Check(snapshot != INVALID_HANDLE_VALUE, "Enumerate target modules");
    MODULEENTRY32W module = {};
    module.dwSize = sizeof(module);
    std::uintptr_t result = 0;
    if (Module32FirstW(snapshot, &module))
        do
        {
            if (!_wcsicmp(module.szModule, name))
            {
                result = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot);
    Check(result != 0, "Find target module");
    return result;
}
static DWORD Invoke(HANDLE process, std::uintptr_t function, void *argument)
{
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(function),
                                       argument, 0, nullptr);
    Check(thread != nullptr, "Start bridge bootstrap thread");
    Check(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0, "Bridge bootstrap timeout");
    DWORD result = 0;
    GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    return result;
}
static void WaitForLoader(PROCESS_INFORMATION &child)
{
    // Stop at the entry point, outside Windows' loader lock, and restore the
    // temporary debugger breakpoint before injecting. The EXE on disk stays
    // byte-identical, and no instruction breakpoint remains during tests.
    DebugSetProcessKillOnExit(FALSE);
    const auto start = GetTickCount64();
    DWORD entryPoint = 0;
    BYTE entryByte = 0;
    bool armed = false;
    const auto writeByte = [&](BYTE byte) {
        DWORD protection;
        Check(VirtualProtectEx(child.hProcess, reinterpret_cast<void *>(entryPoint), 1, PAGE_EXECUTE_READWRITE,
                               &protection) != 0,
              "Protect bootstrap breakpoint");
        Check(WriteProcessMemory(child.hProcess, reinterpret_cast<void *>(entryPoint), &byte, 1, nullptr) != 0,
              "Write bootstrap breakpoint");
        VirtualProtectEx(child.hProcess, reinterpret_cast<void *>(entryPoint), 1, protection, &protection);
        FlushInstructionCache(child.hProcess, reinterpret_cast<void *>(entryPoint), 1);
    };
    for (;;)
    {
        DEBUG_EVENT event = {};
        Check(GetTickCount64() - start < 15000, "Retail loader timeout");
        if (!WaitForDebugEvent(&event, 1000))
            continue;
        bool ready = false;
        DWORD disposition = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT)
        {
            entryPoint = reinterpret_cast<DWORD>(event.u.CreateProcessInfo.lpBaseOfImage) + 0xc5e88;
            if (event.u.CreateProcessInfo.hFile)
                CloseHandle(event.u.CreateProcessInfo.hFile);
        }
        if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile)
            CloseHandle(event.u.LoadDll.hFile);
        if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
            throw std::runtime_error("Retail exited during loader initialization");
        if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
        {
            const auto &exception = event.u.Exception.ExceptionRecord;
            std::cerr << "Bootstrap exception " << std::hex << exception.ExceptionCode << " at "
                      << reinterpret_cast<DWORD>(exception.ExceptionAddress) << " entry " << entryPoint << " thread "
                      << event.dwThreadId << "/" << child.dwThreadId << std::dec << std::endl;
            if (exception.ExceptionCode == EXCEPTION_BREAKPOINT && !armed)
            {
                Check(ReadProcessMemory(child.hProcess, reinterpret_cast<void *>(entryPoint), &entryByte, 1, nullptr) !=
                          0,
                      "Read entry instruction");
                Check(entryByte != 0xcc, "Entry instruction is already patched");
                writeByte(0xcc);
                armed = true;
            }
            else if (exception.ExceptionCode == EXCEPTION_BREAKPOINT &&
                     reinterpret_cast<DWORD>(exception.ExceptionAddress) == entryPoint)
            {
                writeByte(entryByte);
                CONTEXT context = {};
                context.ContextFlags = CONTEXT_CONTROL;
                Check(GetThreadContext(child.hThread, &context) != 0, "Read entry-point registers");
                context.Eip = entryPoint;
                Check(SetThreadContext(child.hThread, &context) != 0, "Restore entry-point instruction pointer");
                Check(SuspendThread(child.hThread) != DWORD(-1), "Suspend initialized UI thread");
                ready = true;
            }
            else
                disposition = DBG_EXCEPTION_NOT_HANDLED;
        }
        Check(ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition) != 0, "Continue retail loader");
        if (ready)
        {
            Check(DebugActiveProcessStop(child.dwProcessId) != 0, "Detach bootstrap debugger");
            return;
        }
    }
}
int wmain(int argc, wchar_t **argv)
{
    PROCESS_INFORMATION child = {};
    try
    {
        if (argc != 5 && argc != 6)
            throw std::runtime_error("Usage: retail-oracle-launch EXE DLL CONTENT_ROOT MOVIE [V3DMM_ENVIRONMENT]");
        const bool v3dmm = argc == 6;
        const char *expectedHash = v3dmm ? V3dmmSHA256 : ExeSHA256;
        Check(Hash(argv[1]) == expectedHash, "Unsupported EXE hash for the selected oracle profile");
        const auto native = [](const wchar_t *path) {
            return std::filesystem::absolute(path).make_preferred().wstring();
        };
        const std::wstring executable = native(argv[1]), dll = native(argv[2]), movie = native(argv[4]),
                           root = native(argv[3]);
        Check(movie.size() < 256 && std::filesystem::is_regular_file(movie),
              "Retail movie path is missing or too long");
        BOOL substituted = FALSE;
        Check(WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, movie.c_str(), -1, nullptr, 0, nullptr, &substituted) !=
                      0 &&
                  !substituted,
              "Retail movie path cannot be represented in the Windows ANSI code page");
        const auto slash = executable.find_last_of(L"/\\");
        const auto directory = executable.substr(0, slash);
        // Retail splits the document path at the final backslash itself.
        // Forward slashes otherwise accepted by Windows lose the directory.
        std::wstring command = L"\"" + executable + L"\" -m \"" + movie + L"\"";
        if (v3dmm)
        {
            const auto environment = native(argv[5]);
            Check(std::filesystem::is_regular_file(environment), "Missing v3dmm environment");
            // Exact historical handoff expected by the unmodified VFS.dll.
            command = L"\"" + executable + L"\" JONSPROWISGAY \"" + environment + L"\"";
        }
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        Check(CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, FALSE, DEBUG_ONLY_THIS_PROCESS, nullptr,
                             directory.c_str(), &startup, &child) != 0,
              "Launch isolated retail EXE");
        WaitForLoader(child);
        void *memory = VirtualAllocEx(child.hProcess, nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        Check(memory != nullptr && (dll.size() + 1) * sizeof(wchar_t) <= 4096, "Allocate bootstrap argument");
        Check(WriteProcessMemory(child.hProcess, memory, dll.c_str(), (dll.size() + 1) * sizeof(wchar_t), nullptr) != 0,
              "Copy bridge path");
        const auto loader = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HMODULE owner = nullptr;
        Check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(loader), &owner) != 0,
              "Locate loader owner");
        wchar_t ownerPath[MAX_PATH];
        GetModuleFileNameW(owner, ownerPath, MAX_PATH);
        const wchar_t *ownerName = wcsrchr(ownerPath, L'\\');
        ownerName = ownerName ? ownerName + 1 : ownerPath;
        const auto remoteLoader = RemoteModule(child.dwProcessId, ownerName) +
                                  reinterpret_cast<std::uintptr_t>(loader) - reinterpret_cast<std::uintptr_t>(owner);
        const auto module = Invoke(child.hProcess, remoteLoader, memory);
        Check(module != 0, "Load oracle DLL");
        HMODULE local = LoadLibraryExW(argv[2], nullptr, DONT_RESOLVE_DLL_REFERENCES);
        Check(local != nullptr, "Inspect oracle export");
        const auto start = GetProcAddress(local, "OracleStart");
        Check(start != nullptr, "Find OracleStart export");
        const auto remoteStart =
            module + reinterpret_cast<std::uintptr_t>(start) - reinterpret_cast<std::uintptr_t>(local);
        FreeLibrary(local);
        Config config;
        config.uiThread = child.dwThreadId;
        config.profile = v3dmm ? 1 : 0;
        Check(root.size() < 260, "Content root is too long");
        wcscpy_s(config.contentRoot, root.c_str());
        Check(WriteProcessMemory(child.hProcess, memory, &config, sizeof(config), nullptr) != 0,
              "Copy bridge configuration");
        const auto initialized = Invoke(child.hProcess, remoteStart, memory);
        if (initialized)
            throw std::runtime_error("Oracle initialization returned " + std::to_string(initialized));
        VirtualFreeEx(child.hProcess, memory, 0, MEM_RELEASE);
        Check(ResumeThread(child.hThread) != DWORD(-1), "Start retail UI");
        FILETIME created, exited, kernel, user;
        Check(GetProcessTimes(child.hProcess, &created, &exited, &kernel, &user) != 0, "Read process identity");
        std::cout << "{\"pid\":" << child.dwProcessId << ",\"thread\":" << child.dwThreadId
                  << ",\"created\":" << ((std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime)
                  << ",\"exe_sha256\":\"" << expectedHash << "\",\"profile\":\""
                  << (v3dmm ? "v3dmm-014-us" : "retail-iso") << "\"}" << std::endl;
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        return 0;
    }
    catch (const std::exception &error)
    {
        // This launcher owns this suspended test child; it never attaches to an existing user instance.
        if (child.hProcess)
        {
            TerminateProcess(child.hProcess, 10);
            CloseHandle(child.hProcess);
        }
        if (child.hThread)
            CloseHandle(child.hThread);
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
