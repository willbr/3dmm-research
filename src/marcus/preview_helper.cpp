#include "preview_worker.h"
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    BrowserPreview preview;
    try { preview = ReadBrowserPreview(argv[1], argv[2]); }
    catch (const std::exception &e) { preview.error = e.what(); }
    return WritePreview(std::cout, preview) ? 0 : 1;
}
