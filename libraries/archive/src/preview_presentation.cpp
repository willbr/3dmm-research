#include <algorithm>
#include <codecvt>
#include <locale>
#include <cstring>
#include "preview_presentation.h"
#include "platform_text.h"
#undef size
ASSERTNAME
namespace ThreeMM
{
namespace Internal
{
namespace
{
std::string FontName(String &name)
{
    auto encoding = sizeof(achar) == 1 ? KauaiText::Encoding::WindowsAnsi : KauaiText::Encoding::Utf16LE;
    auto count = KauaiText::ToUtf16(name.Psz(), name.Cch() * sizeof(achar), encoding, nullptr, 0);
    std::u16string text(count, 0);
    if (count)
        KauaiText::ToUtf16(name.Psz(), name.Cch() * sizeof(achar), encoding,
                           reinterpret_cast<std::uint16_t *>(&text[0]), count);
    return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>().to_bytes(text);
}
bool Caption(PChunkyFile file, ChunkNumber number, PreviewCaption &out)
{
    TextBoxData box;
    ChunkIdentification child;
    RichTextData text;
    if (!box.FRead(file, number, &child) || !text.FRead(file, child.ctg, child.cno) || text.text.cb < 0 ||
        text.text.cb > 1024 * 1024)
        return false;
    out.firstFrame = box.firstFrame;
    out.endFrame = box.maxFrame;
    out.left = box.bounds.xpLeft;
    out.right = box.bounds.xpRight;
    out.top = box.bounds.ypTop;
    out.bottom = box.bounds.ypBottom;
    out.story = box.story;
    out.fontSize = text.defaults.dypFont;
    out.background = text.defaults.lwAcrBack;
    out.font = FontName(text.fontName);
    std::vector<byte> bytes(text.text.cb);
    if (!bytes.empty() && !text.text.FReadRgb(bytes.data(), text.text.cb, 0))
        return false;
    auto encoding = static_cast<KauaiText::Encoding>(text.textOsk);
    auto count = KauaiText::ToUtf16(bytes.data(), bytes.size(), encoding, nullptr, 0);
    if (!bytes.empty() && !count)
        return false;
    out.text.resize(count);
    if (count && !KauaiText::ToUtf16(bytes.data(), bytes.size(), encoding, out.text.data(), count))
        return false;
    for (LegacyLong i = 0; i < text.properties->IvMac(); ++i)
    {
        RichTextPropertyEntry entry;
        text.properties->Get(i, &entry);
        if (text.propertyBo == kboOther)
            SwapBytesRglw(&entry, 2);
        PreviewTextProperty property;
        property.kind = entry.spcp >> 24;
        property.position = entry.spcp & 0xffffff;
        property.value = entry.lw;
        if (property.position > count)
            return false;
        if (property.kind == 2 && entry.lw > 0)
        {
            auto index = entry.lw - 1;
            if (!text.arguments || index >= text.arguments->IvMac() || text.arguments->FFree(index) ||
                text.arguments->Cb(index) < 6)
                return false;
            std::vector<byte> argument(text.arguments->Cb(index));
            text.arguments->Get(index, argument.data());
            String name;
            if (!name.FSetData(argument.data() + 6, static_cast<LegacyLong>(argument.size() - 6)))
                return false;
            property.font = FontName(name);
        }
        out.properties.push_back(std::move(property));
    }
    std::stable_sort(
        out.properties.begin(), out.properties.end(),
        [](const PreviewTextProperty &a, const PreviewTextProperty &b) { return a.position < b.position; });
    return true;
}
} // namespace
bool ReadPresentation(PChunkyFile file, ChunkNumber scene, PreviewPresentation &out)
{
    out = {};
    SceneData metadata;
    if (!metadata.FRead(file, scene))
        return false;
    out.transition = metadata.transition;
    for (int start = 1; start >= 0; --start)
    {
        ChildChunkIdentification child;
        DataBlock block;
        if (!file->FGetKidChidCtg(kctgScen, scene, start, start ? kctgStartGg : kctgFrmGg, &child) ||
            !file->FFind(child.cki.ctg, child.cki.cno, &block))
            return false;
        short order;
        auto group = SceneData::PggReadEvents(&block, &order, start != 0);
        if (!group)
            return false;
        bool ok = true;
        for (LegacyLong i = 0; ok && i < group->IvMac(); ++i)
        {
            SceneEvent event;
            group->GetFixed(i, &event);
            if (order == kboOther)
                SwapBytesBom(&event, kbomSev);
            if (event.sevt == sevtAddTbox)
            {
                std::uint32_t slot;
                group->Get(i, &slot);
                if (order == kboOther)
                    SwapBytesRglw(&slot, 1);
                PreviewCaption caption;
                ok = file->FGetKidChidCtg(kctgScen, scene, slot, kctgTbox, &child) &&
                     Caption(file, child.cki.cno, caption);
                if (ok)
                    out.captions.push_back(std::move(caption));
            }
            else if (event.sevt == sevtPause)
            {
                SceneEventPause pause;
                group->Get(i, &pause);
                if (order == kboOther)
                    SwapBytesRglw(&pause, 2);
                ok = pause.wit >= witNil && pause.wit < witLim && pause.dts >= 0;
                if (ok)
                    out.pauses.push_back({event.nfrm, pause.wit, pause.dts});
            }
            else if (event.sevt == sevtPlaySnd)
            {
                std::vector<std::int32_t> data(group->Cb(i) / 4);
                group->Get(i, data.data());
                if (order == kboOther)
                    SwapBytesRglw(data.data(), static_cast<LegacyLong>(data.size()));
                for (int index = 0; index < data[3]; ++index)
                {
                    auto pair = &data[4 + index * 5]; // chid, sid, runtime-zero, ctg, cno
                    PreviewSoundCue cue;
                    cue.frame = event.nfrm;
                    cue.child = pair[0];
                    cue.source = pair[1];
                    cue.sound = {static_cast<std::uint32_t>(pair[3]), static_cast<std::uint32_t>(pair[4])};
                    cue.style = data[1];
                    cue.volume = data[0];
                    cue.loop = data[2] != 0;
                    cue.queue = index != 0;
                    out.sounds.push_back(cue);
                }
            }
        }
        ReleasePpo(&group);
        if (!ok)
            return false;
    }
    return true;
}
bool ReadAudio(PChunkyFile file, ChunkTagOrType type, ChunkNumber number, PreviewAudio &out)
{
    out = {};
    DataBlock block;
    ChildChunkIdentification child;
    if (type == kctgMsnd)
    {
        // MovieSoundFile has a byte-order/OS marker and three 32-bit fields.
        byte header[16];
        if (!file->FFind(type, number, &block) || !block.FUnpackData() || block.Cb() != 16 ||
            !block.FReadRgb(header, 16, 0))
            return false;
        short order;
        std::memcpy(&order, header, 2);
        if (order != kboCur && order != kboOther)
            return false;
        std::int32_t invalid;
        std::memcpy(&invalid, header + 12, 4);
        if (order == kboOther)
            SwapBytesRglw(&invalid, 1);
        if (invalid || !file->FGetKidChid(type, number, 0, &child))
        {
            out.silent = true;
            return true;
        }
        type = child.cki.ctg;
        number = child.cki.cno;
    }
    if (!file->FFind(type, number, &block) || !block.FUnpackData() || block.Cb() < 0 || block.Cb() > 64 * 1024 * 1024)
        return false;
    out.midi = type == kctgMidi;
    out.asset = {type, number};
    if (out.midi)
    {
        auto stream = MidiStream::PmidsRead(&block);
        if (!stream)
            return false;
        MidiStreamParser parser;
        parser.Init(stream);
        ReleasePpo(&stream);
        MIDEV event;
        while (parser.FGetEvent(&event))
        {
            if (out.events.size() >= 1000000 || event.ts > 24u * 60 * 60 * 1000)
                return false;
            if (event.cb > 0 && event.cb <= 3 && event.rgbSend[0] < 0xf0)
                out.events.push_back({event.ts, std::uint32_t(event.rgbSend[0]) |
                                                    (std::uint32_t(event.rgbSend[1]) << 8) |
                                                    (std::uint32_t(event.rgbSend[2]) << 16)});
        }
    }
    else
    {
        out.wave.resize(block.Cb());
        if (!out.wave.empty() && !block.FReadRgb(out.wave.data(), block.Cb(), 0))
            return false;
    }
    return true;
}
} // namespace Internal
} // namespace ThreeMM
