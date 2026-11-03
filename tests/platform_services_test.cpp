/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
// Deliberately includes no legacy or Windows headers.
#include "core_platform.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

static void Check(bool ok)
{
    if (!ok)
    {
        std::fputs("Portable platform check failed\n", stderr);
        std::exit(1);
    }
}

static std::uint32_t ticks;
static std::uint32_t FakeTicks()
{
    return ticks;
}

int main()
{
    using namespace KauaiPlatform;
    auto *bytes = static_cast<unsigned char *>(Allocate(16, true));
    Check(bytes != nullptr && AllocationSize(bytes) == 16);
    Check(reinterpret_cast<std::uintptr_t>(bytes) % alignof(std::max_align_t) == 0);
    for (int i = 0; i < 16; ++i)
        Check(bytes[i] == 0);
    std::memset(bytes, 0x17, 16);
    // Deterministic overflow rejection exercises the real allocator failure
    // contract without requesting an enormous amount of virtual memory.
    Check(Reallocate(bytes, (std::numeric_limits<std::size_t>::max)()) == nullptr);
    Check(AllocationSize(bytes) == 16 && bytes[0] == 0x17 && bytes[15] == 0x17);
    auto *grown = static_cast<unsigned char *>(Reallocate(bytes, 64));
    Check(grown != nullptr && AllocationSize(grown) == 64);
    for (int i = 0; i < 16; ++i)
        Check(grown[i] == 0x17);
    bytes = static_cast<unsigned char *>(Reallocate(grown, 4));
    Check(bytes != nullptr && AllocationSize(bytes) == 4 && bytes[3] == 0x17);
    void *empty = Reallocate(bytes, 0);
    Check(empty != nullptr && AllocationSize(empty) == 0);
    Free(empty);
    empty = Allocate(0);
    Check(empty != nullptr);
    Free(empty);
    Free(nullptr);
    Check(Allocate((std::numeric_limits<std::size_t>::max)()) == nullptr);

    Mutex mutex;
    unsigned total = 0;
    std::array<long, 4> tokens{};
    std::array<std::thread, 4> threads;
    for (unsigned i = 0; i < threads.size(); ++i)
        threads[i] = std::thread([&, i] {
            tokens[i] = ThreadToken();
            Check(tokens[i] == ThreadToken());
            for (unsigned j = 0; j < 1000; ++j)
            {
                mutex.Enter();
                mutex.Enter(); // Required by nested legacy allocator checks.
                ++total;
                mutex.Leave();
                mutex.Leave();
            }
        });
    for (auto &thread : threads)
        thread.join();
    Check(total == 4000);
    for (unsigned i = 0; i < tokens.size(); ++i)
    {
        Check(tokens[i] != ThreadToken());
        for (unsigned j = 0; j < i; ++j)
            Check(tokens[i] != tokens[j]);
    }

    auto previous = SetTickSource(FakeTicks);
    ticks = 0xfffffff0;
    const auto before = SystemTicks();
    ticks = 0x10;
    Check(static_cast<std::uint32_t>(SystemTicks() - before) == 32);
    Check(SetTickSource(previous) == FakeTicks);
    const auto real = SystemTicks();
    Check(static_cast<std::uint32_t>(SystemTicks() - real) < 0x80000000);
    return 0;
}
