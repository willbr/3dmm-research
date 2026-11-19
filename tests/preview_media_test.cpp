#include "preview_presentation.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#undef size
#include "preview_admission_fixture.h"
ASSERTNAME
struct MediaTrace : AdmissionTrace
{
    MediaTrace() : AdmissionTrace("MM_PREVIEW_MEDIA_SNAPSHOT")
    {
    }
    bool Text(const std::string &s)
    {
        unsigned n = unsigned(s.size());
        return Record(&n, 4) && Record(s.data(), n);
    }
    bool Audio(bool ok, const MM::PreviewAudio &a)
    {
        const unsigned values[] = {ok,     a.asset.type, a.asset.number,          unsigned(a.source),
                                   a.midi, a.silent,     unsigned(a.wave.size()), unsigned(a.events.size())};
        if (!Record(values, sizeof(values)) || !Record(a.wave.data(), a.wave.size()))
            return false;
        for (const auto &e : a.events)
        {
            const unsigned v[] = {e.milliseconds, e.message};
            if (!Record(v, sizeof(v)))
                return false;
        }
        return true;
    }
    bool Caption(bool ok, const MM::PreviewPresentation &p)
    {
        const unsigned values[] = {ok, unsigned(p.transition), unsigned(p.captions.size()), unsigned(p.sounds.size()),
                                   unsigned(p.pauses.size())};
        if (!Record(values, sizeof(values)))
            return false;
        for (const auto &c : p.captions)
        {
            const unsigned v[] = {unsigned(c.firstFrame),
                                  unsigned(c.endFrame),
                                  unsigned(c.left),
                                  unsigned(c.top),
                                  unsigned(c.right),
                                  unsigned(c.bottom),
                                  unsigned(c.fontSize),
                                  c.story,
                                  c.background,
                                  unsigned(c.text.size()),
                                  unsigned(c.properties.size())};
            if (!Record(v, sizeof(v)) || !Text(c.font) || !Record(c.text.data(), c.text.size() * 2))
                return false;
            for (const auto &property : c.properties)
            {
                const unsigned q[] = {property.kind, property.position, property.value};
                if (!Record(q, sizeof(q)) || !Text(property.font))
                    return false;
            }
        }
        return true;
    }
};
static bool Add(PChunkyFile file, ChunkTagOrType type, const std::vector<byte> &bytes, ChunkNumber *number)
{
    return file->FAddPv(const_cast<byte *>(bytes.data()), LegacyLong(bytes.size()), type, number);
}
static bool Caption(PChunkyFile file, int kind, ChunkNumber *scene)
{
    RichTextData text;
    TextBoxData box;
    ChunkIdentification rich;
    std::vector<byte> bytes = {'A', 0, 'B', '\r'};
    if (kind == 1)
    {
        bytes = {'A', 0, 0x3d, 0xd8, 0, 0xde, 'B', 0};
        text.textOsk = 0x0505;
    }
    if (kind == 4 || kind == 5)
        text.textOsk = 0x1111;
    if (kind == 5)
        bytes.clear();
    if (kind == 9)
    {
        bytes = {0, 'A', 0, 'B', 0, 'C', 0, 'D'};
        text.textOsk = 0x0404;
        text.propertyBo = kboOther;
    }
    text.fontName.SetSzs(PszLit("Arial"));
    text.defaults.dypFont = 16;
    text.defaults.dxpDef = 32;
    text.defaults.lwAcrBack = 0xfe00006c;
    text.text.pfil = FileObject::PfilCreateTemp();
    text.text.cb = LegacyLong(bytes.size());
    text.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
    if (!text.text.pfil || !text.properties || !text.text.pfil->FSetFpMac(text.text.cb) ||
        (!bytes.empty() && !text.text.FWriteRgb(bytes.data(), text.text.cb, 0)))
        return false;
    std::vector<RichTextPropertyEntry> entries = {{0x03000002, 0x010000ff}, {0x01000000, 1}, {0x03000002, 0x0100ff00}};
    if (kind == 2)
        entries.push_back({0x01000005, 1});
    if (kind == 1 || kind == 3 || kind == 6 || kind == 7)
        entries.push_back({0x02000000, 1});
    if (kind == 5)
        entries = {{0x01000000, 1}};
    if (kind == 8)
    {
        entries.push_back({0x02000002, 0});
        entries.push_back({0x02000002, -1});
    }
    if (kind == 1 || kind == 6 || kind == 7)
    {
        String font;
        font.SetSzs(PszLit("Courier New"));
        std::vector<byte> argument(6 + font.CbData(), 0);
        font.GetData(argument.data() + 6);
        if (kind == 6)
            argument.resize(5);
        if (kind == 7)
        {
            argument.resize(7);
            argument[6] = 0xff;
        }
        text.arguments = AllocatedGroup::PagNew(sizeof(int32_t));
        LegacyLong index, references = 0x02000001;
        if (!text.arguments || !text.arguments->FAdd(LegacyLong(argument.size()), &index, argument.data(), &references))
            return false;
    }
    for (auto entry : entries)
    {
        if (kind == 9)
            SwapBytesRglw(&entry, 2);
        if (!text.properties->FAdd(&entry))
            return false;
    }
    box.state.first = -3;
    box.state.end = 7;
    box.state.story = true;
    box.SetBounds(-8, -4, 24, 32);
    ChunkNumber number;
    SceneData metadata;
    metadata.firstFrame = -3;
    metadata.lastFrame = 6;
    metadata.transition = transFadeToWhite;
    if (!text.FWrite(file, &rich) || !file->FAdd(0, kctgTbox, &number) || !box.FWrite(file, number) ||
        !file->FAdoptChild(kctgTbox, number, rich.ctg, rich.cno, 0) || !metadata.FCreate(file, scene) ||
        !file->FAdoptChild(kctgScen, *scene, kctgTbox, number, 0))
        return false;
    if (kind == 10)
    {
        ChildChunkIdentification raw;
        if (!file->FGetKidChidCtg(rich.ctg, rich.cno, 0, kctgText, &raw))
            return false;
        file->SetPacked(raw.cki.ctg, raw.cki.cno, true);
    }
    auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
    auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
    SceneEvent event = {-3, sevtAddTbox};
    MMU32 slot = 0;
    bool ok = starts && frames && starts->FInsert(0, sizeof(slot), &slot, &event) &&
              SceneData::FWriteEvents(file, *scene, frames, starts);
    ReleasePpo(&starts);
    ReleasePpo(&frames);
    return ok;
}
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    String path;
    path.SetSzs(argv[1]);
    Filename filename;
    if (!filename.FBuildFromPath(&path))
        return 1;
    auto file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    if (!file)
        return 1;
    MediaTrace trace;
    if (!trace.file)
        return 1;
    const auto audio = [&](ChunkTagOrType type, ChunkNumber number, bool expected) {
        MM::PreviewAudio out;
        out.source = 77;
        out.silent = true;
        out.wave = {1, 2, 3};
        bool ok = MM::Internal::ReadAudio(file, type, number, out);
        if (ok != expected)
            std::fprintf(stderr, "Audio case %08X/%u returned %d\n", unsigned(type), unsigned(number), ok);
        return ok == expected && trace.Audio(ok, out);
    };
    ChunkNumber wave, midi, empty, malformed, late, parent;
    std::vector<byte> raw = {1, 2, 3, 0, 0xff},
                      events = {0, 0x90, 60,   100, 10,   61, 90,   0,  0xc0, 5, 0,    0xff, 0x51, 3,
                                7, 0xa1, 0x20, 0,   0xf8, 5,  0x80, 60, 0,    0, 0xff, 0x2f, 0};
    if (!Add(file, kctgWave, raw, &wave) || !audio(kctgWave, wave, true) || !Add(file, kctgMidi, events, &midi) ||
        !audio(kctgMidi, midi, true) || !Add(file, kctgMidi, {}, &empty) || !audio(kctgMidi, empty, true) ||
        !Add(file, kctgMidi, {0, 0x90, 60, 100, 0x81}, &malformed) || !audio(kctgMidi, malformed, true) ||
        !audio(kctgWave, 0xfffffff0U, false))
        return 1;
    byte encoded[5];
    unsigned n = mm_midi_encode_time(86400000, encoded);
    std::vector<byte> timed(encoded, encoded + n);
    timed.insert(timed.end(), {0, 0x90, 60, 100, 1, 0xff, 0x51, 3, 7, 0xa1, 0x20});
    // The first delta is followed directly by a status byte.
    timed.erase(timed.begin() + n);
    if (!Add(file, kctgMidi, timed, &late) || !audio(kctgMidi, late, false))
        return 1;
    for (int kind = 0; kind < 7; ++kind)
    {
        std::vector<byte> header(16, 0);
        header[0] = 1;
        if (kind == 1)
            header[12] = 1;
        if (kind == 2)
        {
            header[0] = 0;
            header[1] = 1;
        }
        if (kind == 3)
        {
            header[0] = 0x22;
            header[1] = 0x33;
        }
        if (kind == 4)
            header.resize(15);
        if (!Add(file, kctgMsnd, header, &parent))
            return 1;
        if (kind != 5 &&
            !file->FAdoptChild(kctgMsnd, parent, kind == 6 ? kctgMidi : kctgWave, kind == 6 ? midi : wave, 0))
            return 1;
        if (!audio(kctgMsnd, parent, kind != 3 && kind != 4))
            return 1;
    }
    for (int kind = 0; kind < 10; ++kind)
    {
        ChunkNumber scene;
        if (!Caption(file, kind, &scene))
            return 1;
        MM::PreviewPresentation out;
        bool ok = MM::Internal::ReadPresentation(file, scene, out);
        bool expected = kind == 0 || kind == 1 || kind == 5 || kind == 8 || kind == 9;
        if (ok != expected)
        {
            std::fprintf(stderr, "Caption case %d returned %d\n", kind, ok);
            return 1;
        }
        if (!trace.Caption(ok, out))
            return 1;
    }
    // Caption TEXT uses its stored bytes. Other caption chunks use decoded payloads.
    ChunkNumber rawTextScene;
    MM::PreviewPresentation rawText;
    if (!Caption(file, 10, &rawTextScene) || !MM::Internal::ReadPresentation(file, rawTextScene, rawText) ||
        rawText.captions.size() != 1 || rawText.captions.front().text != std::vector<std::uint16_t>{'A', 0, 'B', '\r'})
        return 1;
    bool ok = file->FSave(kctgSoc);
    ReleasePpo(&file);
    if (!ok || !trace.Finish("media", 1275, 0x1B3D6107U))
        return 1;
    std::puts("Preview media: envelope/MIDI boundaries, malformed tails, caption text/properties and failure prefixes "
              "passed.");
    return 0;
}
