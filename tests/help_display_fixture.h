#ifndef HELP_DISPLAY_FIXTURE_H
#define HELP_DISPLAY_FIXTURE_H
#pragma push_macro("size")
#undef size
struct HelpDisplayProbe : HelpTextProbe
{
    using HelpTextProbe::HelpTextProbe;
    bool Object(LiveTextTrace &t, unsigned kind, const void *bytes, long count, PGraphicsPort port, CHP chp,
                bool hidden, bool clipped)
    {
        LegacyLong reference;
        if (!_FEnsureInAg(byte(kind), const_cast<void *>(bytes), count, &reference))
            return false;
        GraphicsEnvironment graphics(port);
        RC clip(4, 3, 22, 18), measured(-77, -66, -55, -44);
        if (clipped)
            graphics.ClipRc(&clip);
        HideButtons(hidden);
        bool bounded = _FGetObjectRc(reference - 1, byte(kind), &graphics, &chp, &measured);
        long x = 6;
        bool drawn = _FDrawObject(reference - 1, byte(kind), &graphics, &x, 16, &chp, &clip);
        return t.Word(bounded) && t.Raw(&measured, 16) && t.Word(drawn) && t.Word(x);
    }
};
static bool TestHelpDisplay()
{
    LiveTextTrace t("MM_HELP_DISPLAY_SNAPSHOT");
    auto file = ChunkyFile::PcflCreateTemp();
    auto cache = file ? ChunkyResourceFile::PcrfNew(file, 0) : nullptr;
    auto p = cache ? NewObj HelpDisplayProbe(cache) : nullptr;
    RC bounds(0, 0, 32, 24), image(0, 0, 8, 5);
    auto port = GraphicsPort::PgptNewOffscreen(&bounds, 32);
    byte pixels[40];
    for (unsigned i = 0; i < sizeof(pixels); ++i)
        pixels[i] = i % 3 ? byte(i + 1) : 0;
    auto bitmap = NewMaskedBitmap(pixels, 8, 5, &image, 3, -2, 0);
    if (!p || !p->Init() || !port || !bitmap)
        return false;
    auto palette = DynamicArray::PglNew(sizeof(Color), 256);
    if (!palette || !palette->FSetIvMac(256))
        return false;
    for (int i = 0; i < 256; ++i)
    {
        Color color = {byte(i), byte(255 - i), byte(i ^ 0x55), 0};
        palette->Put(i, &color);
    }
    port->SetOffscreenColors(palette);
    ReleasePpo(&palette);
    DataBlock block;
    ChunkNumber number, button, missing;
    byte empty = 0;
    if (!file->FAdd(MaskedBitmapSize(bitmap), kctgMbmp, &number, &block) || !WriteMaskedBitmap(bitmap, &block) ||
        !file->FAddPv(&empty, 1, kctgGokd, &button) || !file->FAddPv(&empty, 1, kctgGokd, &missing) ||
        !file->FAdoptChild(kctgGokd, button, kctgMbmp, number, 0x10000))
        return false;
    ReleaseMaskedBitmap(&bitmap);
    auto output = port->PrgbLockPixels();
    unsigned count = unsigned(port->CbRow() * 24);
    for (int mode = 0; mode < 10; ++mode)
        for (int flags = 0; flags < 16; ++flags)
        {
            LegacyLong words[] = {kctgMbmp, LegacyLong(number), 123};
            long bytes = 12;
            if (mode == 1)
                words[1] = 99999;
            if (mode >= 2 && mode <= 4)
            {
                words[0] = kctgGokd;
                words[1] = mode == 2 ? button : mode == 3 ? missing : 99999;
            }
            if (mode >= 5 && mode <= 7)
            {
                words[0] = kctgEditControl;
                words[1] = mode == 5 ? 18 : mode == 6 ? 0 : -4;
            }
            if (mode == 8)
                words[0] = '????';
            if (mode == 9)
                bytes = 7;
            CHP chp = {};
            chp.onn = vpappb->OnnDefVariable();
            chp.dypFont = 12;
            chp.grfont = flags & 1 ? fontBoxed : 0;
            chp.acrFore = kacrRed;
            chp.acrBack = flags & 2 ? kacrWhite : kacrClear;
            std::memset(output, 0x55, count);
            if (!p->Object(t, mode == 9 ? 194 : 192, words, bytes, port, chp, (flags & 4) != 0, (flags & 8) != 0))
                return false;
            port->Flush();
            if (!t.Raw(output, count))
                return false;
        }
    port->Unlock();
    ReleasePpo(&port);
    ReleasePpo(&p);
    ReleasePpo(&cache);
    ReleasePpo(&file);
    bool golden = true;
#ifdef _WIN64
#ifdef KAUAI_SDL3
    golden = t.bytes == 496000 && t.hash == 0xB1CE247DU;
#else
    golden = t.bytes == 496000 && t.hash == 0x79151715U;
#endif
#endif
    return t.Finish("Help display", false) && golden;
}
#pragma pop_macro("size")
#endif
