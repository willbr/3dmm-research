/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "studio_content.h"
#include "studio_missing_content.h"
#ifdef _WINDOWS_
#error Studio content API must not include the Windows SDK
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#include "studio_content_win.h"
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
using namespace StudioContent;
#define Check(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "Content check failed at line %d\n", __LINE__);                                       \
            return false;                                                                                              \
        }                                                                                                              \
    } while (false)
static Text T(const char *text)
{
    Text value;
    while (*text)
        value += Char(static_cast<unsigned char>(*text++));
    return value;
}
static bool Policy()
{
    auto products = ParseProducts("# installed source IDs must match the movies\r\n\r\n1\t3D Movie "
                                  "Maker/3DMovie\r\n002\tExpansion/EXP\n0x3\tThird/3\n");
    Check(products.status == Status::Ready && products.entries.size() == 3);
    Check(products.entries[0].sourceId == T("1") && products.entries[0].title == T("3D Movie Maker/3DMovie"));
    Check(products.entries[1].sourceId == T("002") && products.entries[2].sourceId == T("0x3"));
    Check(ParseProducts("").status == Status::Ready && ParseProducts("# empty").entries.empty());
    Check(ParseProducts("1\t" + std::string(255, 'A')).status == Status::Ready);
    Check(ParseProducts(std::string(255, '1') + "\tA").status == Status::Ready);
    const std::string invalid[] = {"1 Title",
                                   "\tTitle",
                                   "1\t",
                                   "1\tA\tB",
                                   "1\tA\rB",
                                   "1\t" + std::string(256, 'A'),
                                   std::string(256, '1') + "\tA",
                                   std::string("1\tA\0B", 5),
                                   std::string(1024 * 1024 + 1, '#')};
    for (const auto &text : invalid)
    {
        products = ParseProducts("1\tValid first row\n" + text);
        Check(products.status == Status::Invalid && products.entries.empty());
    }
    // Titles and source IDs remain opaque; the application retains the original
    // String::FGetLw conversion (including legacy base/sign rules).
    products = ParseProducts("+1\tCaf\xe9/CAFE");
    Check(products.entries[0].title == T("Caf\xe9/CAFE"));
    Check(ValidateRoot({}, false).status == Status::Ready);
    Check(ValidateRoot(T("/content with spaces"), false).status == Status::Ready);
    Check(ValidateRoot(T("relative"), false).status == Status::Invalid);
    Check(ValidateRoot(T("C:\\Kids"), false).status == Status::Invalid);
    Check(ValidateRoot(T("C:\\Kids"), true).status == Status::Ready);
    Check(ValidateRoot(T("C:/Kids"), true).status == Status::Ready);
    Check(ValidateRoot(T("\\\\server\\share"), true).status == Status::Ready);
    Check(ValidateRoot(T("C:Kids"), true).status == Status::Invalid);
    Check(ValidateRoot(T("\\Kids"), true).status == Status::Invalid);
    Check(ValidateRoot(T("/Kids"), true).status == Status::Invalid);
    Check(ValidateRoot(T(("/" + std::string(254, 'a')).c_str()), false).status == Status::Ready);
    Check(ValidateRoot(T(("/" + std::string(255, 'a')).c_str()), false).status == Status::Invalid);
    Check(ValidateRoot(Text{Char('/'), Char(0), Char('x')}, false).status == Status::Invalid);
    return true;
}
#ifdef _WIN32
#ifdef UNICODE
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
static const NativeChar *N(const Text &text)
{
    return reinterpret_cast<const NativeChar *>(text.c_str());
}
static bool Platform()
{
    const auto key = T(("Software\\3DMMForever\\Tests\\Content-" + std::to_string(GetCurrentProcessId()) + "-" +
                        std::to_string(GetTickCount64()))
                           .c_str());
    struct Fixture
    {
        Text path;
        HKEY handle = nullptr;
        bool owned = false;
        ~Fixture()
        {
            if (handle)
                RegCloseKey(handle);
            if (owned)
                RegDeleteKeyEx(HKEY_CURRENT_USER, N(path), KEY_WOW64_32KEY, 0);
        }
    } fixture{key};
    DWORD disposition = 0;
    Check(RegCreateKeyEx(HKEY_CURRENT_USER, N(key), 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, nullptr,
                         &fixture.handle, &disposition) == ERROR_SUCCESS);
    fixture.owned = disposition == REG_CREATED_NEW_KEY;
    Check(fixture.owned);
    auto products = ReadRegistryProducts(key.c_str(), false);
    Check(products.status == Status::Ready && products.entries.empty());
    const auto put = [&](const Text &name, const Text &title, DWORD type, bool nul) {
        return RegSetValueEx(fixture.handle, N(name), 0, type, reinterpret_cast<const BYTE *>(title.c_str()),
                             static_cast<DWORD>((title.length() + (nul ? 1 : 0)) * sizeof(Char))) == ERROR_SUCCESS;
    };
    Check(put(T("1"), T("Base/B"), REG_SZ, true));
    Check(put(T("not a source ID"), T("Invalid ID kept for Studio to filter"), REG_SZ, true));
    Check(put(T("000002"), T("Longer title after short or invalid ID/SECOND"), REG_SZ, true));
    Check(put(T("3"), T("Unterminated/U"), REG_SZ, false));
    Check(put(T("4"), Text(255, Char('X')), REG_SZ, true));
    Check(put(T("5"), Text(256, Char('X')), REG_SZ, true));
    Check(put(Text(256, Char('6')), T("Oversized name"), REG_SZ, true));
    Check(put(T("7"), T("Not a string"), REG_BINARY, true));
    Check(put(T("8"), T("Still returned after bad values"), REG_SZ, true));
    products = ReadRegistryProducts(key.c_str(), false);
    Check(products.status == Status::Ready && products.entries.size() == 6);
    bool base = false, second = false, unterminated = false, maximum = false, last = false;
    for (const auto &product : products.entries)
    {
        if (product.sourceId == T("1"))
            base = product.title == T("Base/B");
        if (product.sourceId == T("000002"))
            second = product.title == T("Longer title after short or invalid ID/SECOND");
        if (product.sourceId == T("3"))
            unterminated = product.title == T("Unterminated/U");
        if (product.sourceId == T("4"))
            maximum = product.title == Text(255, Char('X'));
        if (product.sourceId == T("8"))
            last = product.title == T("Still returned after bad values");
    }
    Check(base && second && unterminated && maximum && last);
    products = ReadRegistryProducts((key + T("\\missing")).c_str(), false);
    Check(products.status == Status::Error && products.error == ERROR_FILE_NOT_FOUND && products.entries.empty());
    Check(ReadRegistryProducts(nullptr, false).status == Status::Invalid);
    return true;
}
static bool Environment()
{
    const auto name = TEXT("THREEDMM_CONTENT_ROOT");
    const DWORD length = GetEnvironmentVariable(name, nullptr, 0);
    std::basic_string<NativeChar> saved(length, NativeChar(0));
    if (length)
        GetEnvironmentVariable(name, &saved[0], length);
    struct Restore
    {
        const NativeChar *name, *value;
        ~Restore()
        {
            SetEnvironmentVariable(name, value);
        }
    } restore{name, length ? saved.c_str() : nullptr};
    Check(SetEnvironmentVariable(name, nullptr));
    Check(ConfiguredRoot().status == Status::Ready && ConfiguredRoot().path.empty());
    Check(SetEnvironmentVariable(name, TEXT("C:\\Content with spaces")));
    Check(ConfiguredRoot().path == T("C:\\Content with spaces"));
    Check(SetEnvironmentVariable(name, TEXT("relative")));
    Check(ConfiguredRoot().status == Status::Invalid);
    Check(SetEnvironmentVariable(name, N(Text(256, Char('a')))));
    Check(ConfiguredRoot().status == Status::Invalid);
    return true;
}
#else
static bool Platform()
{
    char directory[] = "/tmp/3dmm-content-XXXXXX";
    Check(mkdtemp(directory) != nullptr);
    const std::string root(directory), path = root + "/products.tsv";
    struct Cleanup
    {
        const std::string &root, &path;
        ~Cleanup()
        {
            unlink(path.c_str());
            rmdir(path.c_str());
            rmdir(root.c_str());
        }
    } cleanup{root, path};
    auto products = ReadProducts(root.c_str());
    Check(products.status == Status::Ready && products.entries.empty());
    const auto write = [&](const std::string &text) {
        FILE *file = std::fopen(path.c_str(), "wb");
        if (!file)
            return false;
        const bool ok = std::fwrite(text.data(), 1, text.length(), file) == text.length();
        return std::fclose(file) == 0 && ok;
    };
    Check(write("1\t3D Movie Maker/3DMovie\n2\tExpansion/EXP\n"));
    products = ReadProducts(root.c_str());
    Check(products.status == Status::Ready && products.entries.size() == 2);
    Check(products.entries[1].sourceId == "2" && products.entries[1].title == "Expansion/EXP");
    Check(write("1\tValid\n2 malformed"));
    products = ReadProducts(root.c_str());
    Check(products.status == Status::Invalid && products.entries.empty());
    Check(write(std::string(1024 * 1024 + 1, '#')));
    Check(ReadProducts(root.c_str()).status == Status::Invalid);
    Check(unlink(path.c_str()) == 0 && mkdir(path.c_str(), 0700) == 0);
    Check(ReadProducts(root.c_str()).status == Status::Invalid);
    Check(rmdir(path.c_str()) == 0 && mkfifo(path.c_str(), 0600) == 0);
    Check(ReadProducts(root.c_str()).status == Status::Invalid); // Must not block waiting for a writer.
    Check(unlink(path.c_str()) == 0);
    Check(ReadProducts((root + "/absent").c_str()).status == Status::Error);
    Check(ReadProducts(nullptr).status == Status::Invalid);
    Check(ReadProducts("relative").status == Status::Invalid);
    return true;
}
static bool Environment()
{
    const char *previous = std::getenv("THREEDMM_CONTENT_ROOT");
    const std::string saved = previous ? previous : "";
    struct Restore
    {
        bool set;
        const std::string &saved;
        ~Restore()
        {
            if (set)
                setenv("THREEDMM_CONTENT_ROOT", saved.c_str(), 1);
            else
                unsetenv("THREEDMM_CONTENT_ROOT");
        }
    } restore{previous != nullptr, saved};
    Check(unsetenv("THREEDMM_CONTENT_ROOT") == 0);
    Check(ConfiguredRoot().status == Status::Ready && ConfiguredRoot().path.empty());
    Check(setenv("THREEDMM_CONTENT_ROOT", "/content with spaces", 1) == 0);
    Check(ConfiguredRoot().path == "/content with spaces");
    Check(setenv("THREEDMM_CONTENT_ROOT", "relative", 1) == 0);
    Check(ConfiguredRoot().status == Status::Invalid);
    Check(setenv("THREEDMM_CONTENT_ROOT", std::string(256, '/').c_str(), 1) == 0);
    Check(ConfiguredRoot().status == Status::Invalid);
    return true;
}
#endif
static bool MissingContentMessages()
{
    MissingContent content{"Expansions", "EXPANSIONS", "C:/content/Microsoft Kids", 5, true, 0x424b4744, 0x40003};
    const auto message = MissingContentText(content);
    Check(PackName(content) == "Frankie's 3dmm Expansion Pack 1");
    Check(message.find("C:/content/Microsoft Kids/EXPANSIONS") != std::string::npos);
    Check(message.find("BKGD:00040003") != std::string::npos && message.find("ID 5") != std::string::npos);
    content.sourceId = 918; // Pack names must not depend on installation-specific IDs.
    Check(PackName(content) == "Frankie's 3dmm Expansion Pack 1");
    content.folder = "DORAEMON";
    Check(PackName(content) == "Doraemon Expansion");
    content.folder = "N3DMMExp";
    Check(PackName(content) == "Nickelodeon Expansion");
    content.folder = "3DMovie";
    Check(PackName(content) == "3D Movie Maker");
    content.folder = "EXPANSIONS";
    content.assetNumber = 0xdeadbeef;
    Check(PackName(content) == "Expansions (legacy add-ons)");
    content.source = "My custom pack";
    content.folder = "CUSTOM";
    content.hasAsset = false;
    Check(PackName(content) == "My custom pack");
    Check(MissingContentText(content).find("Asset:") == std::string::npos);
    return true;
}
int main()
{
    if (!Policy() || !Platform() || !Environment() || !MissingContentMessages())
        return 1;
    std::puts("Studio content checks passed");
    return 0;
}
