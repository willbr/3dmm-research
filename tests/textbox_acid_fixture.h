// Authored text-box playback diagnostic. New project code.
#pragma once

namespace TextAcid
{
struct Box
{
    const char *text;
    int left, top, right, bottom;
    const char *font = "Arial";
    int size = 16, style = 0, alignment = 0;
    int foreground = 0, background = 0xffffff;
    int first = 1, end = 61;
    bool scrolling = false, mixed = false;
};
bool WriteBox(PChunkyFile file, const Box &spec, ChunkNumber *number)
{
    RichTextData text;
    text.fontName.SetSzs(const_cast<char *>(spec.font));
    text.defaults.dypFont = spec.size;
    text.defaults.dxpDef = spec.right - spec.left;
    text.defaults.lwAcrBack = spec.background < 0 ? -1 : (0x01000000 | spec.background);
    text.text.pfil = FileObject::PfilCreateTemp();
    text.text.cb = static_cast<LegacyLong>(std::strlen(spec.text));
    text.properties = DynamicArray::PglNew(sizeof(RichTextPropertyEntry));
    if (!text.text.pfil || !text.properties || !text.text.pfil->FSetFpMac(text.text.cb) ||
        !text.text.FWriteRgb(const_cast<char *>(spec.text), text.text.cb, 0))
        return false;
    std::vector<RichTextPropertyEntry> properties = {
        {0x01000000, spec.style}, {0x03000000, 0x01000000 | spec.foreground}, {0x80000000, spec.alignment << 24}};
    if (spec.mixed)
    {
        const std::string content(spec.text);
        const char *labels[] = {"Bold", "Italic", "Both", "Large", "Courier"};
        const int styles[] = {1, 2, 3, 8 << 16, 0};
        for (int i = 0; i < 5; ++i)
            properties.push_back({0x01000000u | static_cast<uint32_t>(content.find(labels[i])), styles[i]});
        // Font arguments use the original rich-text allocated-group wire format.
        String font;
        font.SetSzs(PszLit("Courier New"));
        std::vector<byte> argument(6 + font.CbData(), 0);
        short osk = koskCur;
        std::memcpy(argument.data() + 4, &osk, 2);
        font.GetData(argument.data() + 6);
        text.arguments = AllocatedGroup::PagNew(sizeof(int32_t));
        LegacyLong index, references = 0x02000001;
        if (!text.arguments ||
            !text.arguments->FAdd(static_cast<LegacyLong>((argument.size)()), &index, argument.data(), &references))
            return false;
        properties.push_back({0x02000000u | static_cast<uint32_t>(content.find("Courier")), index + 1});
        properties.push_back({0x03000000u | static_cast<uint32_t>(content.find("Bold")), 0x01ff0000});
        properties.push_back({0x03000000u | static_cast<uint32_t>(content.find("Italic")), 0x01008000});
        properties.push_back({0x03000000u | static_cast<uint32_t>(content.find("Both")), 0x010000ff});
    }
    std::sort(properties.begin(), properties.end(), [](const auto &a, const auto &b) { return a.spcp < b.spcp; });
    for (auto &property : properties)
        if (!text.properties->FAdd(&property))
            return false;
    ChunkIdentification rich;
    TextBoxData box;
    box.firstFrame = spec.first;
    box.maxFrame = spec.end;
    box.story = !spec.scrolling;
    box.bounds.Set(spec.left, spec.top, spec.right, spec.bottom);
    return text.FWrite(file, &rich) && file->FAdd(0, kctgTbox, number) && box.FWrite(file, *number) &&
           file->FAdoptChild(kctgTbox, *number, rich.ctg, rich.cno, 0);
}
bool Write(PChunkyFile file)
{
    const int clear = -1; // kluAcrClear
    std::vector<std::vector<Box>> pages = {
        {{"TEXT ACID / 1: TYPOGRAPHY", 12, 8, 532, 38, "Arial", 20, 1},
         {"Arial 12: Aa 0123!?", 12, 46, 180, 86, "Arial", 12},
         {"Times 16: Aa 0123!?", 190, 46, 358, 86, "Times New Roman", 16},
         {"Courier 20: Aa 0123", 368, 46, 532, 86, "Courier New", 20},
         {"Normal | Bold | Italic | Both | Large | Courier", 12, 94, 532, 144, "Arial", 16, 0, 0, 0, 0xffffff, 1, 61,
          false, true},
         {"RED on white", 12, 152, 180, 190, "Arial", 16, 1, 0, 0xff0000},
         {"GREEN on black", 190, 152, 358, 190, "Arial", 16, 1, 0, 0x00ff00, 0},
         {"BLUE on yellow", 368, 152, 532, 190, "Arial", 16, 1, 0, 0x0000ff, 0xffff00},
         {"LEFT\rsecond line", 12, 198, 180, 250, "Arial", 16, 0, 0},
         {"CENTER\rsecond line", 190, 198, 358, 250, "Arial", 16, 0, 2},
         {"RIGHT\rsecond line", 368, 198, 532, 250, "Arial", 16, 0, 1},
         {"Transparent background: scenery must remain visible", 12, 260, 532, 300, "Arial", 16, 1, 0, 0xffffff,
          clear}},
        {{"TEXT ACID / 2: GEOMETRY + LIFETIME", 12, 8, 532, 38, "Arial", 18, 1},
         {"Narrow box: the same words wrap differently after resizing.\rNew paragraph.", 12, 46, 178, 164},
         {"Wide box: the same words wrap differently after resizing.\rNew paragraph.", 190, 46, 532, 108},
         {"Spaces: A  B   C\rTab:\tD\rABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 190, 116, 532, 170, "Courier New", 12},
         {"BACK BOX: blue, white letters", 12, 178, 330, 226, "Arial", 16, 1, 0, 0xffffff, 0x0000ff},
         {"FRONT BOX: yellow, black letters", 100, 202, 440, 250, "Arial", 16, 1, 0, 0, 0xffff00},
         {"Transparent overlay", 150, 226, 440, 260, "Arial", 16, 2, 0, 0xff0000, clear},
         {"CLIP: line one\rline two\rline three\rline four", 12, 266, 178, 295, "Arial", 16},
         {"Frames 1-19", 300, 266, 532, 300, "Arial", 16, 1, 2, 0xffffff, 0xff0000, 1, 20},
         {"Frames 20-39", 300, 266, 532, 300, "Arial", 16, 1, 2, 0, 0x00ff00, 20, 40},
         {"Frames 40-60", 300, 266, 532, 300, "Arial", 16, 1, 2, 0xffffff, 0x0000ff, 40, 61}},
        {{"TEXT ACID / 3: SCROLLING CREDITS", 12, 8, 532, 38, "Arial", 20, 1},
         {"LEFT 1\rLEFT 2\rLEFT 3\rLEFT 4\rLEFT 5\rLEFT 6\rLEFT 7\rLEFT END", 12, 60, 258, 140, "Arial", 20, 1, 2,
          0xffffff, 0, 3, 31, true},
         {"RIGHT 1\rRIGHT 2\rRIGHT 3\rRIGHT 4\rRIGHT 5\rRIGHT 6\rRIGHT 7\rRIGHT END", 286, 60, 532, 140,
          "Times New Roman", 20, 2, 1, 0xffff00, clear, 3, 31, true},
         {"Two columns scroll upward, clipped to their boxes.\rBoth disappear at frame 31.\rRewind and replay: credits "
          "must restart.",
          12, 184, 532, 292, "Arial", 16}},
        {{"TEXT ACID / 4: MISSING FONT", 12, 8, 532, 38, "Arial", 20, 1},
         {"Missing font sample: Aa Bb 0123456789 !?\rThese words must remain readable.", 12, 54, 532, 126,
          "3DMM_Acid_Missing_7F5C2A", 18},
         {"Arial control sample: Aa Bb 0123456789 !?\rThese words must remain readable.", 12, 142, 532, 214, "Arial",
          18},
         {"Requested font: 3DMM_Acid_Missing_7F5C2A\rIntentionally absent. Compare fallback with retail.\rThe test "
          "does not install a substitute font.",
          12, 232, 532, 300, "Arial", 12}}};
    ChunkNumber root;
    if (!MovieData::FCreateRoot(file, &root))
        return false;
    for (unsigned page = 0; page < (pages.size)(); ++page)
    {
        SceneData scene;
        scene.firstFrame = 1;
        scene.lastFrame = 60;
        scene.transition = transCut;
        ChunkNumber number;
        auto frames = GeneralGroup::PggNew(sizeof(SceneEvent));
        auto starts = GeneralGroup::PggNew(sizeof(SceneEvent));
        bool ok = frames && starts && scene.FCreate(file, &number);
        TAGOnFile background = {2, 0, kctgBkgd, 0};
        SceneEvent event = {1, sevtSetBkgd};
        ok = ok && starts->FInsert(0, sizeof(background), &background, &event);
        for (unsigned slot = 0; ok && slot < (pages[page].size)(); ++slot)
        {
            ChunkNumber box;
            event.sevt = sevtAddTbox;
            ok = WriteBox(file, pages[page][slot], &box) && file->FAdoptChild(kctgScen, number, kctgTbox, box, slot) &&
                 starts->FInsert(starts->IvMac(), sizeof(slot), &slot, &event);
        }
        ok = ok && SceneData::FWriteEvents(file, number, frames, starts) &&
             file->FAdoptChild(kctgMvie, root, kctgScen, number, page);
        ReleasePpo(&frames);
        ReleasePpo(&starts);
        if (!ok)
            return false;
    }
    auto sources = StringTable_GST::PgstNew(sizeof(LegacyLong));
    auto roll = StringTable_GST::PgstNew(sizeof(RollCallActorEntryOnFile));
    String source;
    source.SetSzs(PszLit("3D Movie Maker/3DMOVIE"));
    LegacyLong sid = 2;
    ChunkNumber child;
    DataBlock block;
    bool ok = sources && roll && sources->FAddStn(&source, &sid) &&
              file->FAddChild(kctgMvie, root, 0, roll->CbOnFile(), kctgGst, &child, &block) && roll->FWrite(&block) &&
              file->FAddChild(kctgMvie, root, 1, sources->CbOnFile(), kctgGst, &child, &block) &&
              sources->FWrite(&block) && file->FSave(kctgSoc);
    ReleasePpo(&sources);
    ReleasePpo(&roll);
    return ok;
}
} // namespace TextAcid
