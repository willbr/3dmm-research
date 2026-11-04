/* Copyright (c) 3DMMForever contributors. Licensed under the MIT License. */
#include "mcp_io.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
int failures = 0;
void Check(bool ok, const char *message)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
// Redirect only this test's inherited handles, restoring them before exit.
struct Pipe
{
#ifdef _WIN32
    HANDLE input = nullptr, output = nullptr, saved = nullptr;
    DWORD target;
    explicit Pipe(bool stdinPipe) : target(stdinPipe ? STD_INPUT_HANDLE : STD_OUTPUT_HANDLE)
    {
        Check(CreatePipe(&input, &output, nullptr, 0) != 0, "create pipe");
        saved = GetStdHandle(target);
        SetStdHandle(target, stdinPipe ? input : output);
    }
    void CloseWriter()
    {
        if (output)
            CloseHandle(output);
        output = nullptr;
    }
    ~Pipe()
    {
        SetStdHandle(target, saved);
        if (input)
            CloseHandle(input);
        CloseWriter();
    }
    void Feed(const char *data, std::size_t length)
    {
        DWORD written = 0;
        Check(WriteFile(output, data, static_cast<DWORD>(length), &written, nullptr) && written == length,
              "feed stdin");
    }
    void CloseReader()
    {
        if (input)
            CloseHandle(input);
        input = nullptr;
    }
    std::size_t Read(char *data, std::size_t capacity)
    {
        DWORD got = 0;
        ReadFile(input, data, static_cast<DWORD>(capacity), &got, nullptr);
        return got;
    }
#else
    int input = -1, output = -1, saved = -1, target;
    explicit Pipe(bool stdinPipe) : target(stdinPipe ? STDIN_FILENO : STDOUT_FILENO)
    {
        int handles[2];
        Check(pipe(handles) == 0, "create pipe");
        input = handles[0];
        output = handles[1];
        saved = dup(target);
        Check(dup2(stdinPipe ? input : output, target) == target, "redirect stdio");
    }
    void CloseWriter()
    {
        if (output >= 0)
            close(output);
        output = -1;
    }
    ~Pipe()
    {
        dup2(saved, target);
        close(saved);
        if (input >= 0)
            close(input);
        CloseWriter();
    }
    void Feed(const char *data, std::size_t length)
    {
        Check(write(output, data, length) == static_cast<ssize_t>(length), "feed stdin");
    }
    void CloseReader()
    {
        if (input >= 0)
            close(input);
        input = -1;
    }
    std::size_t Read(char *data, std::size_t capacity)
    {
        const auto got = read(input, data, capacity);
        return got > 0 ? static_cast<std::size_t>(got) : 0;
    }
#endif
};
} // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--png") == 0)
    {
        // Asymmetric rows/colors catch flipped images and channel swaps;
        // enough rows to cross the encoder's stored-DEFLATE block boundary.
        std::vector<std::uint32_t> pixels;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 128; ++x)
                pixels.push_back(0xff000000u | (x << 16) | (y << 8) | ((x + y) & 255));
        const auto png = mcp::EncodePng(pixels, 128, 200);
        return mcp::WriteStdout(reinterpret_cast<const char *>(png.data()), png.size()) ? 0 : 1;
    }
    Check(mcp::EncodePng({}, 0, 1).empty(), "reject zero size");
    Check(mcp::EncodePng({}, -1, 1).empty(), "reject negative size");
    Check(mcp::EncodePng({1}, 2, 1).empty(), "reject mismatched framebuffer");
    Check(mcp::EncodePng({}, 2147483647, 2147483647).empty(), "reject overflow dimensions");
    const auto png = mcp::EncodePng({0xff123456}, 1, 1);
    Check(png.size() > 50 && png[0] == 137 && png[12] == 'I' && png[25] == 2, "RGB PNG header");
    {
        Pipe input(true);
        char buffer[4096];
        std::size_t received = 123;
        const auto start = std::chrono::steady_clock::now();
        Check(mcp::ReadStdin(buffer, sizeof(buffer), received) == mcp::ReadStatus::timeout && received == 0,
              "open idle pipe times out");
        Check(std::chrono::steady_clock::now() - start < std::chrono::seconds(1), "bounded shutdown polling");
        const std::string message = "{\"id\":1,\"text\":\"caf\xc3\xa9\"}\r\n";
        input.Feed(message.data(), 3);
        Check(mcp::ReadStdin(buffer, sizeof(buffer), received) == mcp::ReadStatus::data && received == 3 &&
                  std::memcmp(buffer, message.data(), 3) == 0,
              "fragmented input");
        input.Feed(message.data() + 3, message.size() - 3);
        Check(mcp::ReadStdin(buffer, sizeof(buffer), received) == mcp::ReadStatus::data &&
                  received == message.size() - 3 && std::memcmp(buffer, message.data() + 3, received) == 0,
              "UTF-8 and CRLF bytes preserved");
        input.CloseWriter();
        Check(mcp::ReadStdin(buffer, sizeof(buffer), received) == mcp::ReadStatus::end, "stdin EOF");
    }
    {
        Pipe output(false);
        std::string payload(256 * 1024, 'x'), actual;
        std::thread reader([&] {
            char buffer[2048];
            while (actual.size() < payload.size())
            {
                auto got = output.Read(buffer, sizeof(buffer));
                if (!got)
                    break;
                actual.append(buffer, got);
            }
        });
        Check(mcp::WriteStdout(payload.data(), payload.size()), "write beyond pipe capacity");
        reader.join();
        Check(actual == payload, "stdout has no truncation");
        output.CloseReader();
        Check(!mcp::WriteStdout("x", 1), "disconnected stdout returns error without SIGPIPE termination");
    }
    std::fprintf(stderr, "MCP IO: %s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
