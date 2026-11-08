/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "pack_hash.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace StudioPacks
{
namespace
{
constexpr std::uint32_t constants[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
std::uint32_t Rotate(std::uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}
struct SHA256
{
    std::uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char block[64]{};
    std::size_t used = 0;
    std::uint64_t count = 0;
    void Compress()
    {
        std::uint32_t w[64];
        for (unsigned i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(block[4 * i]) << 24) | (std::uint32_t(block[4 * i + 1]) << 16) |
                   (std::uint32_t(block[4 * i + 2]) << 8) | block[4 * i + 3];
        for (unsigned i = 16; i < 64; ++i)
            w[i] = w[i - 16] + (Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
                   (Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19) ^ (w[i - 2] >> 10));
        auto a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
             h = state[7];
        for (unsigned i = 0; i < 64; ++i)
        {
            auto t1 = h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) + ((e & f) ^ (~e & g)) + constants[i] + w[i];
            auto t2 = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
    void Add(const void *input, std::size_t size)
    {
        auto bytes = static_cast<const unsigned char *>(input);
        count += size;
        while (size)
        {
            auto n = (std::min)(size, 64 - used);
            std::memcpy(block + used, bytes, n);
            used += n;
            bytes += n;
            size -= n;
            if (used == 64)
            {
                Compress();
                used = 0;
            }
        }
    }
    std::string Finish()
    {
        auto bits = count * 8;
        unsigned char tail[128]{};
        tail[0] = 0x80;
        auto padding = used < 56 ? 56 - used : 120 - used;
        for (unsigned i = 0; i < 8; ++i)
            tail[padding + i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        Add(tail, padding + 8);
        std::ostringstream out;
        for (auto word : state)
            out << std::hex << std::setw(8) << std::setfill('0') << word;
        return out.str();
    }
};
} // namespace
std::string Hash(const void *bytes, std::size_t size)
{
    SHA256 hash;
    hash.Add(bytes, size);
    return hash.Finish();
}
std::string HashFile(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot hash file: " + path.u8string());
    SHA256 hash;
    std::array<char, 65536> buffer;
    while (stream)
    {
        stream.read(buffer.data(), buffer.size());
        hash.Add(buffer.data(), std::size_t(stream.gcount()));
    }
    if (!stream.eof())
        throw std::runtime_error("Cannot finish hashing file");
    return hash.Finish();
}
} // namespace StudioPacks
