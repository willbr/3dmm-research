/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef WAVE_FIXTURE_H
#define WAVE_FIXTURE_H
#include <cstdint>
#include <vector>
namespace WaveFixture
{
inline void Word(std::vector<std::uint8_t> &v, std::uint32_t n)
{
    v.push_back(n & 255);
    v.push_back((n >> 8) & 255);
}
inline void Dword(std::vector<std::uint8_t> &v, std::uint32_t n)
{
    Word(v, n);
    Word(v, n >> 16);
}
inline void Chunk(std::vector<std::uint8_t> &v, const char *name, const std::vector<std::uint8_t> &data)
{
    v.insert(v.end(), name, name + 4);
    Dword(v, static_cast<std::uint32_t>(data.size()));
    v.insert(v.end(), data.begin(), data.end());
    if (data.size() & 1)
        v.push_back(0);
}
inline std::vector<std::uint8_t> Wave(const std::vector<std::uint8_t> &format, const std::vector<std::uint8_t> &data,
                                      unsigned fact = 0)
{
    std::vector<std::uint8_t> body = {'W', 'A', 'V', 'E'};
    Chunk(body, "fmt ", format);
    if (fact)
    {
        std::vector<std::uint8_t> count;
        Dword(count, fact);
        Chunk(body, "fact", count);
    }
    Chunk(body, "data", data);
    std::vector<std::uint8_t> result = {'R', 'I', 'F', 'F'};
    Dword(result, static_cast<std::uint32_t>(body.size()));
    result.insert(result.end(), body.begin(), body.end());
    return result;
}
inline std::vector<std::uint8_t> Pcm(const std::vector<std::int16_t> &samples)
{
    std::vector<std::uint8_t> fmt, data;
    Word(fmt, 1);
    Word(fmt, 1);
    Dword(fmt, 48000);
    Dword(fmt, 96000);
    Word(fmt, 2);
    Word(fmt, 16);
    for (auto sample : samples)
        Word(data, static_cast<std::uint16_t>(sample));
    return Wave(fmt, data);
}
inline std::vector<std::uint8_t> Pcm8(const std::vector<std::uint8_t> &samples, unsigned rate)
{
    std::vector<std::uint8_t> fmt;
    Word(fmt, 1);
    Word(fmt, 1);
    Dword(fmt, rate);
    Dword(fmt, rate);
    Word(fmt, 1);
    Word(fmt, 8);
    return Wave(fmt, samples);
}
inline std::vector<std::uint8_t> Adpcm()
{
    std::vector<std::uint8_t> fmt;
    Word(fmt, 2);
    Word(fmt, 1);
    Dword(fmt, 48000);
    Dword(fmt, 96000);
    Word(fmt, 8);
    Word(fmt, 4);
    Word(fmt, 32);
    Word(fmt, 4);
    Word(fmt, 7);
    const int coefficients[] = {256, 0, 512, -256, 0, 0, 192, 64, 240, 0, 460, -208, 392, -232};
    for (int value : coefficients)
        Word(fmt, static_cast<std::uint16_t>(value));
    std::vector<std::uint8_t> data = {0};
    Word(data, 16);
    Word(data, 1024);
    Word(data, std::uint16_t(-1024));
    data.push_back(0);
    return Wave(fmt, data, 4);
}
} // namespace WaveFixture
#endif
