/* Copyright (c) 3DMMForever contributors. Licensed under the MIT License. */
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mcp
{
// Inherited stdio is borrowed, never closed. Read waits at most 50 ms so the
// server can join its reader even when the host keeps stdin open indefinitely.
bool StdioAvailable();
enum class ReadStatus
{
    data,
    timeout,
    end
};
ReadStatus ReadStdin(char *data, std::size_t capacity, std::size_t &received);
bool WriteStdout(const char *data, std::size_t length);

// Top-down XRGB8888 pixels. Produces RGB PNG without platform imaging APIs.
std::vector<unsigned char> EncodePng(const std::vector<std::uint32_t> &pixels, int width, int height);
} // namespace mcp
