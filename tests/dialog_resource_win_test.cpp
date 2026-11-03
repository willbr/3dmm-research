/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "dialog_session.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include "frame.h"
#undef size
ASSERTNAME
void FrameMain(void)
{
}
static void Check(bool value, const char *message)
{
    if (!value)
    {
        std::fprintf(stderr, "Dialog native check: %s (Windows error %lu)\n", message, GetLastError());
        std::exit(1);
    }
}
static INT_PTR CALLBACK DialogProc(HWND, UINT, WPARAM, LPARAM)
{
    return FALSE;
}
static std::u16string WindowText(HWND window)
{
    const auto length = GetWindowTextLengthW(window);
    std::vector<wchar_t> text(length + 1);
    GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
    return std::u16string(text.begin(), text.end() - 1);
}
static std::vector<KauaiDialogs::Binding> Model(HMODULE module, LPCWSTR name, KauaiDialogs::Resource &model)
{
    auto found = FindResourceW(module, name, MAKEINTRESOURCEW(5));
    auto loaded = found ? LoadResource(module, found) : nullptr;
    auto data = loaded ? LockResource(loaded) : nullptr;
    Check(data && KauaiDialogs::ReadResource(data, SizeofResource(module, found), model), "decode compiled dialog");
    std::vector<KauaiDialogs::Binding> bindings;
    Check(KauaiDialogs::BuildBindings(model, bindings), "bind compiled dialog");
    return bindings;
}
static void LegacyBindings(HMODULE module, LPCWSTR name, const std::vector<KauaiDialogs::Binding> &bindings)
{
    if (!IS_INTRESOURCE(name) || bindings.empty())
        return;
    vwig.hinst = module;
    auto dialog = Dialog::PdlgNew(static_cast<long>(reinterpret_cast<UINT_PTR>(name)));
    Check(dialog && dialog->IvMac() == static_cast<long>(bindings.size()), "legacy item count");
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        DialogItem item;
        dialog->GetDit(static_cast<long>(i), &item);
        Check(item.sitMin == bindings[i].first && item.sitLim == bindings[i].limit &&
                  item.ditk == static_cast<long>(bindings[i].kind),
              "legacy callback item range");
        Check(dialog->IditFromSit(item.sitMin) == static_cast<long>(i), "legacy control lookup");
        if (item.ditk == ditkCheckBox)
            Check(!dialog->FGetCheck(static_cast<long>(i)), "initial checkbox value");
        if (item.ditk == ditkRadioGroup)
            Check(dialog->LwGetRadio(static_cast<long>(i)) == 0, "initial radio value");
    }
    ReleasePpo(&dialog);
}
static void NativeControls(HMODULE module, int id)
{
    KauaiDialogs::Resource model;
    auto bindings = Model(module, MAKEINTRESOURCEW(id), model);
    Check(model.extended == (id == 102) && model.font.present, "template format/font");
    Check(model.controls.size() == 7 && model.title == (id == 101 ? u"Standard Resource" : u"Extended Resource"),
          "compiled metadata");
    if (id == 102)
        Check(model.help == 7000 && model.font.points == 9 && model.font.weight == 400 && model.font.charset == 1,
              "extended metadata");
    Check(bindings.size() == (id == 101 ? 5 : 6) && bindings[2].limit - bindings[2].first == 2,
          "original radio grouping");
    auto dialog = CreateDialogParamW(module, MAKEINTRESOURCEW(id), nullptr, DialogProc, 0);
    Check(dialog != nullptr, "create hidden native dialog");
    Check(WindowText(dialog) == model.title, "native caption");
    auto child = GetWindow(dialog, GW_CHILD);
    for (const auto &control : model.controls)
    {
        Check(child != nullptr, "native control count");
        const auto nativeId = GetDlgCtrlID(child);
        Check(control.id == static_cast<std::uint32_t>(nativeId) || (control.id == 65535 && nativeId == -1),
              "native control ID");
        Check(!control.title.ordinal && WindowText(child) == control.title.text, "native control caption");
        Check(control.windowClass.ordinal, "compiled class encoding");
        wchar_t className[64] = {};
        GetClassNameW(child, className, 64);
        const wchar_t *expected = control.windowClass.number == 0x80   ? L"Button"
                                  : control.windowClass.number == 0x81 ? L"Edit"
                                  : control.windowClass.number == 0x82 ? L"Static"
                                                                       : L"ComboBox";
        Check(_wcsicmp(className, expected) == 0, "native control class");
        child = GetWindow(child, GW_HWNDNEXT);
    }
    Check(!child, "extra native controls");
    DestroyWindow(dialog);
    LegacyBindings(module, MAKEINTRESOURCEW(id), bindings);
}
struct Inventory
{
    unsigned dialogs = 0, controls = 0, bindings = 0, sdlSupported = 0;
};
static void NativeCreationData(HMODULE module, bool extended)
{
    // Two controls make the creation-data length observable in User32's
    // parsing of the following control, independently of our reader.
    std::vector<WORD> words;
    const auto dword = [&](DWORD value) {
        words.push_back(LOWORD(value));
        words.push_back(HIWORD(value));
    };
    if (extended)
    {
        words.push_back(1);
        words.push_back(0xFFFF);
        dword(0);
        dword(0);
        dword(WS_POPUP);
    }
    else
    {
        dword(WS_POPUP);
        dword(0);
    }
    words.insert(words.end(), {2, 0, 0, 100, 60, 0, 0, 0});
    for (unsigned item = 0; item < 2; ++item)
    {
        if (words.size() & 1)
            words.push_back(0);
        if (extended)
        {
            dword(0);
            dword(0);
            dword(WS_CHILD | WS_VISIBLE | WS_TABSTOP);
        }
        else
        {
            dword(WS_CHILD | WS_VISIBLE | WS_TABSTOP);
            dword(0);
        }
        words.insert(words.end(), {5, WORD(5 + item * 20), 50, 12});
        if (extended)
            dword(401 + item);
        else
            words.push_back(WORD(401 + item));
        words.insert(words.end(), {0xFFFF, WORD(item ? 0x80 : 0x81), 0});
        words.push_back(item ? 0 : 2);
        if (!item)
            words.push_back(0x1234);
    }
    KauaiDialogs::Resource model;
    Check(KauaiDialogs::ReadResource(words.data(), words.size() * 2, model), "read native creation data fixture");
    Check(model.controls.size() == 2 && model.controls[0].creationData == std::vector<std::uint8_t>({0x34, 0x12}) &&
              model.controls[1].id == 402,
          "creation payload and next-control alignment");
    auto dialog =
        CreateDialogIndirectParamW(module, reinterpret_cast<DLGTEMPLATE *>(words.data()), nullptr, DialogProc, 0);
    Check(dialog && GetDlgItem(dialog, 401) && GetDlgItem(dialog, 402), "native creation-data counts/alignment");
    DestroyWindow(dialog);
}
static BOOL CALLBACK Visit(HMODULE module, LPCWSTR, LPCWSTR name, LONG_PTR value)
{
    auto &inventory = *reinterpret_cast<Inventory *>(value);
    KauaiDialogs::Resource model;
    auto bindings = Model(module, name, model);
    LegacyBindings(module, name, bindings);
    KauaiDialogs::Session session;
    if (session.Init(model)) ++inventory.sdlSupported;
    else std::printf("Retained native dialog template %llu: %s\n",static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(name)),KauaiDialogs::Caption(model.title).c_str());
    if (model.title == u"Product Info")
    {
        // Studio's callback enums rely on these original indices, including
        // the debug-only AV edit field. Pin them independently of BuildBindings.
        bool debug = false;
        for (const auto &control : model.controls)
            if (control.id == 1025)
                debug = true;
        Check(bindings.size() == (debug ? 10 : 9) && bindings[0].first == 2 && bindings[1].first == 1020 &&
                  bindings[2].first == 1015 && bindings[2].limit == 1017 && (!debug || bindings[3].first == 1025) &&
                  bindings[debug ? 4 : 3].first == 1019 && bindings[debug ? 8 : 7].first == 1010 &&
                  bindings[debug ? 9 : 8].first == 1012,
              "original Product Info callback indices");
    }
    ++inventory.dialogs;
    inventory.controls += static_cast<unsigned>(model.controls.size());
    inventory.bindings += static_cast<unsigned>(bindings.size());
    return TRUE;
}
int main(int argc, char **argv)
{
    Check(argc <= 2, "arguments");
    auto module = GetModuleHandleW(nullptr);
    NativeCreationData(module, false);
    NativeCreationData(module, true);
    NativeControls(module, 101);
    NativeControls(module, 102);
    if (argc == 2)
    {
        module = LoadLibraryExA(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        Check(module != nullptr, "load Studio resources without running Studio");
        Inventory inventory;
        Check(EnumResourceNamesW(module, MAKEINTRESOURCEW(5), Visit, reinterpret_cast<LONG_PTR>(&inventory)) != 0 &&
                  inventory.dialogs > 0,
              "enumerate Studio dialogs");
        std::printf("Studio resources: %u dialogs, %u controls and %u original logical items decoded.\n",
                    inventory.dialogs, inventory.controls, inventory.bindings);
        std::printf("SDL dialog template support: %u/%u resources.\n",inventory.sdlSupported,inventory.dialogs);
        FreeLibrary(module);
    }
    std::puts("Compiled dialog resources: standard/extended metadata, native captions/classes/IDs and Kauai callback "
              "bindings passed.");
}
