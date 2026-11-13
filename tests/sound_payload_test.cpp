#include <cstdio>
#include <cstdlib>
#include <vector>
#include "wave_codec.h"
#include "kauai_core.h"
#undef size
ASSERTNAME
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%ld: %s\n", file, static_cast<long>(line), message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "sound payload: %d\n", __LINE__);                                                     \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)
static FILE *snapshot;
static std::uint32_t fingerprint = 2166136261u, recordedBytes = 0;
static void Record(const void *p, std::size_t n)
{
    const auto bytes = static_cast<const byte *>(p);
    for (std::size_t i = 0; i < n; ++i)
        fingerprint = (fingerprint ^ bytes[i]) * 16777619u;
    recordedBytes += static_cast<std::uint32_t>(n);
    if (snapshot)
        CHECK(std::fwrite(p, 1, n, snapshot) == n);
}
static void Word(std::vector<byte> &v, unsigned n)
{
    v.push_back(n >> 8);
    v.push_back(n);
}
static void Chunk(std::vector<byte> &v, const char *tag, const std::vector<byte> &data)
{
    v.insert(v.end(), tag, tag + 4);
    Word(v, static_cast<unsigned>(data.size()) >> 16);
    Word(v, static_cast<unsigned>(data.size()));
    v.insert(v.end(), data.begin(), data.end());
}
static void Midi()
{
    // Include tempo changes, ignored meta/sysex, running status, track ordering,
    // sequential files and SMPTE divisions. Capture both imported bytes and peeks.
    for (unsigned format : {0u, 1u, 2u, 3u})
        for (unsigned division : {1u, 96u, 480u, 0xE728u, 0xE350u})
            for (unsigned variant = 0; variant != 8; ++variant)
            {
                std::vector<byte> header, file;
                Word(header, format);
                Word(header, 2);
                Word(header, division);
                Chunk(file, "MThd", header);
                Chunk(file, "skip", {0, 1, 2});
                std::vector<byte> first{
                    0,    0xC0, byte(variant), 0,  0x90, 60,   64,   10,   61, 63,   0,    0xFF, 1,  2,    'x', 'y',
                    0,    0xFF, 0x51,          3,  7,    0xA1, 0x20, 0x81, 0,  0x80, 60,   0,    0,  0xF0, 2,   0x7E,
                    0xF7, 0,    0x90,          62, 32,   5,    0xFF, 0x51, 3,  3,    0xD0, 0x90, 12, 0x80, 62,  0,
                    0,    0xFF, 0x2F,          0};
                if (variant & 1)
                    first[7] = 0x81; // alter velocity, accepted by legacy parser
                if (variant & 2)
                    first.resize(first.size() - 4); // EOF instead of EOT
                if (variant & 4)
                    first.insert(first.begin(), {0, 0xFF, 1, 0});
                Chunk(file, "MTrk", first);
                Chunk(file, "MTrk", {0, 0x90, 40, 90, 20, 0x80, 40, 0, 0, 0xFF, 0x2F, 0});
                auto source = FileObject::PfilCreateTemp();
                CHECK(source && source->FWriteRgb(file.data(), static_cast<LegacyLong>(file.size()), 0));
                Filename name;
                source->GetFni(&name);
                auto stream = MidiStream::PmidsReadNative(&name);
                CHECK(stream);
                const auto count = stream->CbOnFile();
                CHECK(count > 0);
                Record(&count, sizeof(count));
                auto chunks = ChunkyFile::PcflCreateTemp();
                ChunkNumber number;
                DataBlock block;
                CHECK(chunks && chunks->FAdd(count, 'MIDS', &number, &block) && stream->FWrite(&block));
                std::vector<byte> raw(count);
                CHECK(block.FReadRgb(raw.data(), count, 0));
                Record(raw.data(), raw.size());
                {
                    MidiStreamParser parser;
                    parser.Init(stream, 123, 500000);
                    MIDEV event;
                    unsigned events = 0;
                    while (parser.FGetEvent(&event, false))
                    {
                        Record(&event, sizeof(event));
                        CHECK(parser.FGetEvent(&event));
                        Record(&event, sizeof(event));
                        CHECK(++events < 100);
                    }
                }
                block.Free();
                ReleasePpo(&chunks);
                ReleasePpo(&stream);
                ReleasePpo(&source);
            }
}
static void Wave()
{
    unsigned state = 73;
    for (unsigned count : {1u, 2u, 499u, 500u, 501u, 1500u, 1501u, 11025u})
        for (unsigned pattern = 0; pattern != 5; ++pattern)
            for (unsigned rate : {11025u, 44100u})
            {
                std::vector<byte> samples(count), result;
                for (unsigned i = 0; i < count; ++i)
                {
                    state = state * 1664525u + 1013904223u;
                    samples[i] = pattern == 0   ? 128
                                 : pattern == 1 ? 0
                                 : pattern == 2 ? (i & 1 ? 255 : 0)
                                 : pattern == 3 ? byte(i)
                                                : byte(state >> 24);
                }
                CHECK(KauaiAudio::AdpcmWave(samples, rate, result));
                Record(result.data(), result.size());
                CHECK(KauaiAudio::Pcm8Wave(samples, rate, result));
                Record(result.data(), result.size());
            }
}
int main()
{
    if (const auto path = std::getenv("THREE_MM_SOUND_PAYLOAD_SNAPSHOT"))
    {
        snapshot = std::fopen(path, "wb");
        CHECK(snapshot);
    }
    Midi();
    Wave();
    // Frozen from the native parser/importer/encoder before their conversion.
    // Event metadata in this trace uses native byte order; wire bytes do not.
    const unsigned endian = 1;
    if (*reinterpret_cast<const byte *>(&endian))
        CHECK(recordedBytes == 314180 && fingerprint == 0x9323c635u);
    if (snapshot)
        CHECK(!std::fclose(snapshot));
    std::puts("Sound payloads: MIDI import/merge/peek and exact PCM/ADPCM encoding passed.");
}
