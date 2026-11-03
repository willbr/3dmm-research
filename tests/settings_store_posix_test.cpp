/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "settings_store_posix.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <vector>
#include <dirent.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace KauaiSettings;
#define Check(ok)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(ok))                                                                                                     \
            throw std::runtime_error("POSIX settings check failed at line " + std::to_string(__LINE__));               \
    } while (false)
struct Environment
{
    std::string name, previous;
    bool existed;
    Environment(const char *key, const std::string &value) : name(key), existed(std::getenv(key) != nullptr)
    {
        if (existed)
            previous = std::getenv(key);
        Check(setenv(key, value.c_str(), 1) == 0);
    }
    ~Environment()
    {
        if (existed)
            setenv(name.c_str(), previous.c_str(), 1);
        else
            unsetenv(name.c_str());
    }
};
static void RemoveTree(const std::string &path)
{
    struct stat info;
    if (lstat(path.c_str(), &info) != 0)
        return;
    if (!S_ISDIR(info.st_mode))
    {
        unlink(path.c_str());
        return;
    }
    auto dir = opendir(path.c_str());
    if (!dir)
        return;
    while (auto entry = readdir(dir))
        if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, ".."))
            RemoveTree(path + '/' + entry->d_name);
    closedir(dir);
    rmdir(path.c_str());
}
struct TemporaryRoot
{
    std::string path;
    TemporaryRoot()
    {
        char pattern[] = "/tmp/3mm-settings-XXXXXX";
        auto created = mkdtemp(pattern);
        Check(created != nullptr);
        path = created;
    }
    ~TemporaryRoot()
    {
        RemoveTree(path);
    }
};
static std::vector<unsigned char> ReadFile(const std::string &path)
{
    auto file = std::fopen(path.c_str(), "rb");
    Check(file != nullptr);
    std::vector<unsigned char> result;
    int ch;
    while ((ch = std::fgetc(file)) != EOF)
        result.push_back(static_cast<unsigned char>(ch));
    const bool ok = !std::ferror(file);
    std::fclose(file);
    Check(ok);
    return result;
}
static void WriteFile(const std::string &path, const std::vector<unsigned char> &bytes)
{
    auto file = std::fopen(path.c_str(), "wb");
    Check(file != nullptr);
    const bool written = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    Check(written && closed);
}
static bool ReadExact(int fd, void *data, std::size_t count)
{
    auto p = static_cast<unsigned char *>(data);
    while (count)
    {
        const auto done = read(fd, p, count);
        if (done < 0 && errno == EINTR)
            continue;
        if (done <= 0)
            return false;
        count -= done;
        p += done;
    }
    return true;
}
static void ConcurrentDefaults()
{
    int gate[2], results[2];
    Check(pipe(gate) == 0 && pipe(results) == 0);
    pid_t children[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        children[i] = fork();
        Check(children[i] >= 0);
        if (!children[i])
        {
            close(gate[1]);
            close(results[0]);
            char go;
            if (!ReadExact(gate[0], &go, 1))
                _exit(2);
            std::uint32_t value = 100 + i;
            Request request;
            request.setDefault = true;
            if (!Access(PosixBackend(), "Test", "Race", &value, sizeof(value), request) ||
                write(results[1], &value, sizeof(value)) != sizeof(value))
                _exit(3);
            _exit(0);
        }
    }
    close(gate[0]);
    close(results[1]);
    const bool released = write(gate[1], "go!!", 4) == 4;
    close(gate[1]);
    std::uint32_t values[4];
    const bool received = ReadExact(results[0], values, sizeof(values));
    close(results[0]);
    bool exited = true;
    for (auto child : children)
    {
        int status;
        const auto waited = waitpid(child, &status, 0);
        exited = exited && waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    Check(released && received && exited);
    Check(values[0] >= 100 && values[0] < 104);
    for (auto value : values)
        Check(value == values[0]);
}
static void Run()
{
    TemporaryRoot temporary;
    Environment home("HOME", temporary.path + "/home"), config("XDG_CONFIG_HOME", temporary.path + "/config");
#ifdef __APPLE__
    const auto host = PosixHost::MacOS;
#else
    const auto host = PosixHost::Linux;
#endif
    const auto root = ConfigurationRoot(host, false, std::getenv("HOME"), std::getenv("XDG_CONFIG_HOME"));
    const auto directory = root + "/k-74657374";     // ASCII-folded "Test", hex encoded.
    const auto record = directory + "/v-76616c7565"; // "Value".
    const auto &backend = PosixBackend();
    Request request;
    request.setDefault = true;
    std::uint32_t value = 42;
    Check(Access(backend, "Test", "Value", &value, sizeof(value), request));
    value = 99;
    Check(Access(backend, "TEST", "VALUE", &value, sizeof(value), request) && value == 42);
    const auto original = ReadFile(record);
    const unsigned char header[] = {'K', 'P', 'S', '1', 1, 0, 0, 0, 4, 0, 0, 0};
    Check(original.size() == 16 && !std::memcmp(original.data(), header, sizeof(header)) &&
          !std::memcmp(original.data() + 12, &value, 4));
    struct stat info;
    Check(stat(record.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
    Check(stat(directory.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700);
    bool missing = false;
    request.setDefault = false;
    Check(Access(backend, "Test", "Absent", &value, sizeof(value), request, &missing) && missing);
    Check(!Access(backend, "Test", "Absent", &value, sizeof(value), request));
    request.setDefault = true;
    unsigned char shortBuffer = 0xa5;
    Check(!Access(backend, "Test", "Value", &shortBuffer, 1, request, &missing) && !missing && shortBuffer == 0xa5);
    Check(ReadFile(record) == original);

    // Persistent record corruption is an error; a default must never hide it.
    std::vector<std::vector<unsigned char>> corrupt;
    auto bad = original;
    bad.resize(8);
    corrupt.push_back(bad);
    bad = original;
    bad[0] = 'X';
    corrupt.push_back(bad);
    bad = original;
    bad[3] = '9';
    corrupt.push_back(bad);
    bad = original;
    bad[4] = 255;
    corrupt.push_back(bad);
    bad = original;
    bad[5] = 1;
    corrupt.push_back(bad);
    bad = original;
    bad[8] = 7;
    corrupt.push_back(bad);
    bad = original;
    bad.push_back(0);
    corrupt.push_back(bad);
    bad = original;
    bad[8] = 3;
    bad.pop_back();
    corrupt.push_back(bad);
    bad = original;
    bad[4] = 2;
    for (unsigned i = 12; i < 16; ++i)
        bad[i] = 'a';
    corrupt.push_back(bad);
    bad = original;
    bad[11] = 2;
    corrupt.push_back(bad);
    for (const auto &bytes : corrupt)
    {
        WriteFile(record, bytes);
        value = 777;
        missing = true;
        Check(!Access(backend, "Test", "Value", &value, sizeof(value), request, &missing) && !missing && value == 777);
        Check(ReadFile(record) == bytes);
    }
    WriteFile(record, original);
    const auto backup = record + ".backup";
    Check(rename(record.c_str(), backup.c_str()) == 0);
    Check(symlink(backup.c_str(), record.c_str()) == 0);
    Check(!Access(backend, "Test", "Value", &value, sizeof(value), request));
    Check(ReadFile(backup) == original && unlink(record.c_str()) == 0);
    Check(mkfifo(record.c_str(), 0600) == 0);
    Check(!Access(backend, "Test", "Value", &value, sizeof(value), request));
    Check(unlink(record.c_str()) == 0 && rename(backup.c_str(), record.c_str()) == 0);

    request.type = Type::String;
    char text[] = {'a', 0, 'z', 0};
    Check(Access(backend, "Test", "Text", text, sizeof(text), request));
    bool created;
    auto handle = backend.open("Test", false, &created);
    Check(handle != nullptr);
    std::uint32_t count = 0;
    Type type = Type::Unknown;
    const auto queried = backend.read(handle, "Text", nullptr, &count, &type);
    backend.close(handle);
    Check(queried == ReadResult::Found && type == Type::String && count == 2);
    request.write = true;
    Check(Access(backend, "Test", "Text", text, sizeof(text), request));
    char copy[4] = {};
    request.write = false;
    Check(Access(backend, "Test", "Text", copy, sizeof(copy), request) && !std::memcmp(copy, text, sizeof(text)));
    request.type = Type::Binary;
    request.write = true;
    unsigned char blob[] = {0, 255, 42, 0, 17};
    Check(Access(backend, "Test", "Blob", blob, sizeof(blob), request));
    Check(Access(backend, "Test", "EmptyBlob", nullptr, 0, request));
    request.write = false;
    Check(Access(backend, "Test", "EmptyBlob", nullptr, 0, request));
    unsigned char blobCopy[5] = {};
    Check(Access(backend, "Test", "Blob", blobCopy, sizeof(blobCopy), request) &&
          !std::memcmp(blob, blobCopy, sizeof(blob)));

    // Force a partial temporary-file write: the previous record must survive.
    struct rlimit previous, limited;
    Check(getrlimit(RLIMIT_FSIZE, &previous) == 0);
    limited = previous;
    limited.rlim_cur = 64;
    auto previousSignal = signal(SIGXFSZ, SIG_IGN);
    Check(previousSignal != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limited) == 0);
    unsigned char large[256] = {};
    request.write = true;
    const bool written = Access(backend, "Test", "Value", large, sizeof(large), request);
    const bool restored = setrlimit(RLIMIT_FSIZE, &previous) == 0;
    signal(SIGXFSZ, previousSignal);
    Check(!written && restored && ReadFile(record) == original);
    auto dir = opendir(directory.c_str());
    Check(dir != nullptr);
    bool noTemporaries = true;
    while (auto entry = readdir(dir))
        noTemporaries = noTemporaries && std::strncmp(entry->d_name, ".tmp-", 5) != 0;
    closedir(dir);
    Check(noTemporaries);
    Check(!Access(backend, std::string(127, 'x').c_str(), "Value", blob, sizeof(blob), request));
    ConcurrentDefaults();
    request = Request{};
    request.setDefault = true;
    value = 1;
    Check(Access(backend, "Software\\Microsoft\\Microsoft Kids\\3D Movie Maker", "SkipSplashScreen", &value,
                 sizeof(value), request));
    std::puts(
        "POSIX settings: persistence, payloads, corruption, failed-write recovery, locking and isolated paths passed.");
}
int main()
{
    try
    {
        Run();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
