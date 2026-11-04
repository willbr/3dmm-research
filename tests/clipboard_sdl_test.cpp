/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "sdl_host.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include "frame.h"
#undef size
bool SendExternalClipboardEvent();
ASSERTNAME
void FrameMain(void)
{
}
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "Clipboard check failed at %d\n", __LINE__);                                          \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static void Pump()
{
    for (int i = 0; i < 20; ++i)
        vpappb->PumpHostEvent();
}
static bool External(const std::string &text)
{
    auto &host = KauaiSDL::Main();
    if (!host.SetClipboardText(text))
        return false;
    Pump();
    // SDL's dummy backend owns an isolated clipboard. Deliver the same event
    // SDL sends when another application replaces the system clipboard.
    if (!SendExternalClipboardEvent())
        return false;
    Pump();
    return !host.OwnsClipboard();
}
static bool Contents(const std::string &expected)
{
    PDocumentBase document = nullptr;
    if (!vpclip->FGetFormat(kclsTextDocumentBase, &document))
        return false;
    auto text = static_cast<PTextDocumentBase>(document);
    std::vector<achar> buffer(text->CpMac());
    text->FetchRgch(0, text->CpMac(), buffer.data());
    bool matches = buffer.size() == expected.size();
    for (std::size_t i = 0; matches && i < buffer.size(); ++i)
        matches = static_cast<unsigned char>(buffer[i]) == static_cast<unsigned char>(expected[i]);
    if (!matches)
    {
        std::fprintf(stderr, "Clipboard contents: expected %zu units, got %zu:", expected.size(), buffer.size());
        for (auto unit : buffer)
            std::fprintf(stderr, " %02x", static_cast<unsigned>(static_cast<unsigned char>(unit)));
        std::fprintf(stderr, "\n");
    }
    ReleasePpo(&document);
    return matches;
}
int main()
{
    auto &host = KauaiSDL::Main();
    CHECK(host.Create("Clipboard test", 64, 64, true));
    KauaiSDL::Event event;
    while (host.Poll(event))
    {
    }
#ifdef WIN
    vwig.hdcApp = GetDC(nullptr);
    CHECK(vwig.hdcApp);
#endif
    CHECK(vntl.FInit());
    auto app = NewObj ApplicationBase;
    CHECK(app);
    app->HideCurs();
    app->HideCurs();
    app->ShowCurs();
    CHECK(!host.Cursors().visible);
    app->ShowCurs();
    CHECK(host.Cursors().visible);
    auto document = RichTextDocument::PtxrdNew();
    achar sample[] = {'r', 'i', 'c', 'h', ' ', 't', 'e', 'x', 't'};
    CHECK(document && document->FReplaceRgch(sample, 9, 0, 0));
    vpclip->Set(document);
    Pump();
    std::string exported;
    CHECK(host.GetClipboardText(exported) && exported == "rich text" && host.OwnsClipboard());
    vpclip->Import();
    PDocumentBase same = nullptr;
    CHECK(vpclip->FGetFormat(kclsRichTextDocument, &same) && same == document);
    ReleasePpo(&same);
    CHECK(External("Caf\xC3\xA9\nsecond\r\nthird"));
    CHECK(Contents("Caf\xE9\rsecond\rthird\r"));
    CHECK(External(std::string(1400, 'x')) && Contents(std::string(1400, 'x') + '\r'));
    CHECK(External("") && !vpclip->FGetFormat(kclsTextDocumentBase));
    vpclip->Set();
    ReleasePpo(&document);
    ReleasePpo(&app);
#ifdef WIN
    ReleaseDC(nullptr, vwig.hdcApp);
    vwig.hdcApp = nullptr;
#endif
    host.Close();
    std::puts("SDL clipboard: text export, rich document retention, external import, line endings, long text and empty "
              "replacement passed.");
    return 0;
}
