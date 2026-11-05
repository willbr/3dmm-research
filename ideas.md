* point rendering mode
* BBOX rendering mode
* fps multiplier
* remove the word Microsoft from the app
* include MCP server in release build
* improve MCP server
* tidy up files in repo
* include some basic event logging for the last n events to help with debug
* convert assertion failure dialog to a normal dialog, so i cant copy and paste it
* increase the max buffer size in BRender? I want 4k and 1440p in software
* add gizmos
* free flying camera mode (for editing)
* free flying camera mode faked by moving all items in a scene
* hires screenshots? 4k?
* do we need to vendor the brender library?
* onion skinning? render actor path?
* import/export the scene? OBJ? Blender?
* remake the gui using imgui
* background editor in imgui
* actor editor in imgui
* prop editor in imgui
* use zig build?  or just a batch file?
* write some docs about the codebase
    * chunky files
    * 3mm file format
    * brender
    * 3mm file format
    * how the MCP server works
    * how the multi select works

* fix all virtual store problems
* wsprintfA does take the string length, all string functions should
* make names more verbose, same idea as the struct renames:
    1. variable names
    2. method names
    3. function names
* headless mode
    * do we need to split kauai into two for headless?

* add a menu items for:
    * exporting:
        * screenshots
        * gifs
        * videos (scene, movie with transactions)


* render to video to test x86 vs x64
* write a full text plan that covers all features
* find an open source font to use
* design a new sqlite base file format
* rewrite to c, I hate c++
* update to use SIMD
* assets editor
* enable lighting using the pallete
* build a `3mm` CLI using the split core libraries
    * inspect and validate movies, dump JSON, compare movies, extract captions/assets and report dependencies
    * use stable, normalized JSON dumps for semantic regression tests and x86 vs x64 comparisons
    * do not assume load/save produces byte-identical `.3mm` files: physical layout and chunk numbering can change
    * keep normalization in the read-only dump tool; do not change the `.3mm` writer or format just to make tests deterministic
    * retain a strict view of IDs, references and format details so normalization cannot hide compatibility bugs
    * test semantic round trips separately from format versions, fixed-width record layouts and chunk-reference integrity
    * keep legacy movie fixtures and verify newly saved files in the original editor before claiming backwards compatibility

* long-term platform/UI: SDL3 and Dear ImGui, including macOS support
    * run the existing Studio UI and tools through SDL3; preserve Kauai controls and resource scripts
    * use ImGui for auxiliary tools where useful; a replacement Studio is not the porting goal
    * keep SDL3 and ImGui in host adapters; core libraries remain usable by CLI tools and regression tests
    * first remove native dialogs and registry/DLL spelling integration from core, then port allocation, synchronization, timing, files and text
    * isolate GDI regions/metafiles and preserve legacy `.3mm` data and encoding behavior during the migration

* selectable file/asset backends using SDL3 streams
    * support normal folders, direct ISO reads and embedded assets through `SDL_IOStream`
    * reuse the ISO directory parser and expose bounded streams for individual assets, avoiding bulk extraction
    * include file-existence checks and directory enumeration in backend selection; keep movie saves on the writable filesystem
    * allow loose files and expansion packs to override ISO assets through an explicit search order
    * evaluate a thin, generic, SDL-independent interface versus using SDL directly in core libraries and CLI tools
    * keep or rename `KauaiFiles` only where it provides useful contracts; remove unnecessary legacy behavior and redundant wrappers
    * retain caching as an optional fallback for components that still require real filenames, such as font discovery
    * compare startup time, disk usage and runtime reads, and require both backends to pass the same movie and building workflow tests
