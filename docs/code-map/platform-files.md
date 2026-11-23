# Files and native paths

`platform_files.h` is the file/path boundary. It exposes opaque handles,
fixed-width sizes, native path code units and directory records. It imports no
Windows, application, SDL or ImGui headers. Windows and POSIX implementations live in
the C89 `mm/native_files.h` component, with `platform_files_bridge.cpp` adapting
existing C++ callers. The C interface also has no SDK types; its implementation
contains the Win32/shell or POSIX OS calls.

`mm_native_file_read_stream` exposes a borrowed native handle as an
`MMReadStream` for the shared chunky reader. The stream snapshots the file
length at initialization. Each exact read seeks on the handle, so the caller
keeps it open and serializes access. `engine-native-chunky` exercises this path
with the original `bkgds.3cn` using a C-only executable.

`file_native.cpp` and `filename.cpp` adapt the shared C89 file-owner, file-factory,
filename and enumeration controllers. They retain native object/default-context
integration. Filename's type and counted path now use the plain C
`MMFilenameOwned8`/`MMFilenameOwned16` records; the native facade no longer owns
a `String` member. C initialization, prefix-only copying, export and validation
retain native diagnostic ordering. These replace `filewin.cpp` and
`fniwin.cpp`. Native dialogs remain in the GUI library's `fnidialogwin.cpp`.
Enumeration current/suspended frames also use plain C records from
`mm/filename_frame.h`; recursion snapshots contain no C++ Filename or vtable.
Their metadata preserves copied reference counts/diagnostic kinds as value
payload, with iterator ownership still controlled by the C enumerator.

The Windows backend preserves `CreateFile` access/share flags, default ACLs,
no-overwrite `MoveFile`, read-only attributes, native search-path rules, drive
enumeration and hidden/system filtering. Path comparison retains the legacy
equal-length `CharUpperBuff` comparison; locale collation is not substituted.
ANSI paths stay ANSI and Unicode builds use UTF-16. Neither movie strings nor
stored text encodings are converted by the backend.

The Windows C component owns OS handle I/O, attributes, sharing/create modes,
exact transfers, path comparison/query/creation and directory iteration. Signed
32-bit size/position limits remain. A failed resize may still move the file
position before `SetEndOfFile` fails. Current/executable/leaf-query failures clear
nonempty output; full/search/temp queries retain their SDK publication rules.
Directory name writes retain the SDK extent and leave the rest of a supplied
1024-unit buffer untouched. Separate ANSI and UTF-16 C-only link tests, SDK-aware
strict C89 checks, native original/converted traces and AddressSanitizer cover
this boundary; see the [regression log](../tools/playback-regression.md).

Studio startup uses `CurrentDirectory`, `ExecutablePath` and `LookupLeafName`
through this boundary. Leaf lookup retains Windows `FindFirstFile` behavior,
including short-name expansion, case matching, wildcards and trailing quotes.
POSIX checks existence with `lstat` and returns the supplied leaf spelling;
it does not expand wildcards or canonicalize case on case-insensitive volumes.
The lookup never rewrites parent directories. Insufficient output capacity
fails without returning a truncated filename.

`MakeDirectories` creates missing parents for an absolute path and accepts an
existing directory without changing permissions. POSIX creates new parents with
mode 0700; Windows retains shell/default-ACL behavior and accepts either native
separator spelling. `ChangeDirectory` reports native error codes and leaves cwd
unchanged on failure. Both are used by Studio profile startup. Account-name and
app-data-root discovery use the C89 `mm/native_users.h` service, with
`platform_users_bridge.cpp` translating the existing `platform_users.h` values.
The separate `mm-native-users` library has C-only ANSI/UTF-16 Windows and byte-path
POSIX builds. Query results own fixed 256-unit buffers; callers do not free them.
Windows retains WNet's extended-provider error/message and local-app-data root.
POSIX queries the effective uid with a 1 KiB buffer, doubling on `ERANGE` up to
1 MiB; empty/missing/error accounts and names exceeding 255 bytes are unavailable.
Linux uses absolute `XDG_DATA_HOME`, otherwise absolute `HOME/.local/share`;
macOS uses absolute `HOME/Library/Application Support`. No directories are created.
`mm_native_posix_user_data_root` also exposes this path policy on other hosts,
including a measure-only call. Native root queries clear insufficient nonempty
buffers and never publish a truncated path. C++ value allocation remains only
in the compatibility adapter; account lookup and path policy execute in C.

Preference storage uses a separate C89 `mm-native-settings` library and
`mm/native_settings.h`. Windows retains the original registry view and optional
test namespace. POSIX stores the same typed KPS1 payloads under configuration
roots selected by `mm_settings_configuration_root`. Each open holds the namespace
lock until close. Writes retain exclusive temporary creation, exact/retrying I/O,
file sync/close and rename publication; directory sync remains best effort.
The context owns its two-word temporary-number sequence and mutex. Destroy it
after all handles close and all calls finish; coordinate environment changes
and fork with callers stopped. The shared `mm/settings.h` access policy preserves
missing/error/default handling, exact explicit payload sizes and output flags.
`settings_store_bridge.cpp` supplies legacy C++ values/default bindings; its
exception adapters allow C to close handles before rethrowing to C++ callers.

Temporary files now use exclusive creation after choosing a name. A collision
fails rather than truncating a file another process created in the meantime.
Ordinary `PfilCreate` retains its overwrite behavior. Temporary-name selection
still uses the existing bounded search and is not a new reservation API.

Private archive scratch files use `mm_native_file_temporary`. Windows creates
them exclusively with delete-on-close. POSIX creates them with `mkstemp` and
unlinks them before returning the descriptor. The archive owner releases the
handle when the last retained block or archive reference closes.

## POSIX backend

`native_files_posix.c` supplies macOS/Linux file descriptors, native byte
paths, lexical absolute-path normalization, directory iteration and filesystem
operations through `mm/native_files.h`. Both operating-system backends use the
same `platform_files_bridge.cpp` forwarding adapter; the old POSIX C++
implementation is removed. C heap buffers replace strings/vectors and an
explicit directory owner replaces the C++ object. It handles interrupted
read/write/open/lock/flush calls and complete transfers.
File sizes/positions remain limited to signed 32-bit range; this is not a large
file format migration.

Platform policies are explicit:

- `/` is the separator. Backslashes are ordinary filename bytes. Paths are
  compared byte-for-byte without global case folding or Unicode normalization.
  Case-insensitive-volume aliases and symlink identities are not canonicalized.
- Absolute-path normalization collapses `.`/`..` lexically without resolving
  symlinks and works for filenames not yet on disk. Path search uses an explicit
  colon-separated list or `PATH`; temporary paths use `TMPDIR` or `/tmp`.
- Cooperative readers take shared advisory `flock` locks. Writers and deny-read
  opens take exclusive locks. This is stricter than Windows: simultaneous
  reader/writer access and permissive concurrent writers are unsupported, even
  when deny-write is false. Programs bypassing advisory locks are not constrained.
  Locked files cannot be truncated, deleted or renamed through this backend.
- No-overwrite file renames use `linkat`/`unlink` on the same filesystem. An
  existing destination is preserved. This requires hard-link support and is not
  an atomic single-step rename: both names exist briefly. Failure to remove the
  source triggers removal of the new link. The inherited multi-step file-swap
  policy also remains nontransactional.
- Dot names are hidden; there is no Windows system-file attribute. Native root
  enumeration yields `/`, not synthetic drive letters. Removable/CD/network
  classification currently returns no flags.
- Directory symlinks/reparse points can be listed but are not recursively
  traversed, preventing recursive walks from cycling. Dangling file symlinks
  can be listed, renamed and deleted without changing their target.
- A failed `lstat` leaves metadata untouched. Once a symlink has been found,
  metadata is cleared and its link bit is set before the target is queried.
  Target errors such as a symlink loop can therefore publish partial metadata.
  Directory iteration can publish the name and partial metadata on error;
  its handle remains owned by the caller until explicitly closed. The shared
  adapter seeds and copies metadata on every result to retain those rules.
- Full/search/temp failures leave the output and leaf pointer untouched.
  Current-directory, executable-path and leaf-name queries clear nonempty
  output on failure. In-place full-path normalization remains supported.

The complete core still needs GDI/source/LP64 portability work before it builds
on macOS. Text and public native-type boundaries are [documented separately](platform-text.md). The standalone project below checks the new backend independently of
that work; it is not an editor or engine port.

## Verification

The regular Windows `library-checks` target includes `file-services-test` and
`filename-test`. They cover native sharing conflicts, denied truncation,
exclusive create, short reads, resizing, no-overwrite rename, file swaps,
permission-upgrade restoration, temp cleanup, extension handling, directory
filters, recursive pre/post events, skip-directory and reinitialization.
The existing normalized `.3mm` baseline remains a separate compatibility gate.

Two inherited failure-path bugs are fixed: `FSetGrffil` returned true after an
unsuccessful permission upgrade, and popping an empty enumeration stack trashed
its output state in Debug builds. Tests exercise both cases.

Build only the portable memory/timing/file modules without legacy headers:

```console
cmake -S tests/platform -B out/platform -DCMAKE_BUILD_TYPE=Debug
cmake --build out/platform
ctest --test-dir out/platform --output-on-failure
```

The workflow adds Linux and macOS jobs for this standalone project. Windows
checks exercise the C backend through the real core libraries and the standalone
platform project. Linux Debug/Release original-versus-C byte traces, GCC/Clang
strict C89 checks and C-only link checks cover the POSIX boundary; see the
[regression log](../tools/playback-regression.md) for runtime evidence and limits.
Windows Unicode builds must define `UNICODE` consistently for C and C++ sources
(including both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` when using global flags).
No SDL3 or ImGui
dependency is introduced; future host/UI adapters consume the same boundary.
