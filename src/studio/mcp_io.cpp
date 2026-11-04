/* Copyright (c) 3DMMForever contributors. Licensed under the MIT License. */
#include "mcp_io.h"
#include <algorithm>
#include <chrono>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace mcp
{
bool StdioAvailable()
{
#ifdef _WIN32
    const auto input = GetFileType(GetStdHandle(STD_INPUT_HANDLE));
    const auto output = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    return (input == FILE_TYPE_PIPE || input == FILE_TYPE_DISK) && output != FILE_TYPE_UNKNOWN;
#else
    return fcntl(STDIN_FILENO, F_GETFD) != -1 && fcntl(STDOUT_FILENO, F_GETFD) != -1;
#endif
}

ReadStatus ReadStdin(char *data, std::size_t capacity, std::size_t &received)
{
    received = 0;
#ifdef _WIN32
    auto input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD count = static_cast<DWORD>(std::min<std::size_t>(capacity, 65536));
    if (GetFileType(input) == FILE_TYPE_PIPE)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
            return ReadStatus::end;
        if (!available)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return ReadStatus::timeout;
        }
        count = std::min(count, available);
    }
    DWORD got = 0;
    if (!ReadFile(input, data, count, &got, nullptr) || !got)
        return ReadStatus::end;
    received = got;
#else
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const int ready = poll(&input, 1, 50);
    if (!ready || (ready < 0 && errno == EINTR))
        return ReadStatus::timeout;
    if (ready < 0 || (input.revents & (POLLERR | POLLNVAL)))
        return ReadStatus::end;
    const auto got = read(STDIN_FILENO, data, capacity);
    if (got < 0 && (errno == EINTR || errno == EAGAIN))
        return ReadStatus::timeout;
    if (got <= 0)
        return ReadStatus::end;
    received = static_cast<std::size_t>(got);
#endif
    return ReadStatus::data;
}

bool WriteStdout(const char *data, std::size_t length)
{
#ifndef _WIN32
    // A disconnected client must not terminate Studio with SIGPIPE. Block it
    // only on this thread; preserve the application's existing signal policy.
    sigset_t blocked, previous, pending;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &blocked, &previous))
        return false;
    sigpending(&pending);
    const bool alreadyPending = sigismember(&pending, SIGPIPE) == 1;
#endif
    bool ok = true;
    while (length)
    {
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), data, static_cast<DWORD>(std::min<std::size_t>(length, 65536)),
                       &written, nullptr) ||
            !written)
        {
            ok = false;
            break;
        }
#else
        const auto written = write(STDOUT_FILENO, data, std::min<std::size_t>(length, 65536));
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
        {
            ok = false;
            break;
        }
#endif
        data += written;
        length -= written;
    }
#ifndef _WIN32
    if (!alreadyPending)
    {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE) == 1)
        {
            int signal = 0;
            sigwait(&blocked, &signal);
        }
    }
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
#endif
    return ok;
}
} // namespace mcp
