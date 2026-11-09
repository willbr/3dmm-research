#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

// Read ZIP members through the original v3dmm PhysicsFS DLL, independently of
// the port's archive parser. Run with a private archive, never installed content.
int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fputs("Usage: physfs-zip-probe PHYSFS_DLL ARCHIVE MEMBER [MEMBER ...]\n", stderr);
        return 2;
    }
    auto library = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library)
    {
        std::fprintf(stderr, "Cannot load PhysicsFS: %lu\n", GetLastError());
        return 2;
    }
    struct Version
    {
        unsigned char major, minor, patch;
    } version{};
    auto linkedVersion =
        reinterpret_cast<void(__cdecl *)(Version *)>(GetProcAddress(library, "PHYSFS_getLinkedVersion"));
    if (linkedVersion)
    {
        linkedVersion(&version);
        std::printf("PhysicsFS %u.%u.%u\n", version.major, version.minor, version.patch);
    }
    auto init = reinterpret_cast<int(__cdecl *)(const char *)>(GetProcAddress(library, "PHYSFS_init"));
    auto mount = reinterpret_cast<int(__cdecl *)(const char *, int)>(GetProcAddress(library, "PHYSFS_addToSearchPath"));
    auto open = reinterpret_cast<void *(__cdecl *)(const char *)>(GetProcAddress(library, "PHYSFS_openRead"));
    auto read = reinterpret_cast<__int64(__cdecl *)(void *, void *, unsigned, unsigned)>(
        GetProcAddress(library, "PHYSFS_read"));
    auto close = reinterpret_cast<int(__cdecl *)(void *)>(GetProcAddress(library, "PHYSFS_close"));
    auto deinit = reinterpret_cast<int(__cdecl *)()>(GetProcAddress(library, "PHYSFS_deinit"));
    if (!init || !mount || !open || !read || !close || !deinit || !init(argv[0]) || !mount(argv[2], 1))
    {
        auto error = reinterpret_cast<const char *(__cdecl *)()>(GetProcAddress(library, "PHYSFS_getLastError"));
        std::fprintf(stderr, "Cannot mount archive: %s\n", error ? error() : "missing exports");
        return 2;
    }
    for (int i = 3; i < argc; ++i)
    {
        auto file = open(argv[i]);
        std::printf("%s:", argv[i]);
        if (!file)
            std::fputs(" missing", stdout);
        else
        {
            unsigned char bytes[256];
            auto count = read(file, bytes, 1, sizeof(bytes));
            for (__int64 j = 0; j < count; ++j)
                std::printf("%02x", bytes[j]);
            close(file);
        }
        std::putchar('\n');
    }
    deinit();
    FreeLibrary(library);
    return 0;
}
