# Files and native paths

`platform_files.h` is the file/path boundary. It exposes opaque handles,
fixed-width sizes, native path code units and directory records. It imports no
Windows, application, SDL or ImGui headers.

`file_native.cpp` owns the existing `FileObject` lifecycle, cached-file registry,
permission upgrades/restoration, sticky error levels, temporary cleanup and
rename/swap behavior. `filename.cpp` owns extension handling, navigation,
filtering and recursive pre/post enumeration. These replace `filewin.cpp` and
`fniwin.cpp`. Native dialogs remain in the GUI library's `fnidialogwin.cpp`.

The Windows backend preserves `CreateFile` access/share flags, default ACLs,
no-overwrite `MoveFile`, read-only attributes, native search-path rules, drive
enumeration and hidden/system filtering. Path comparison retains the legacy
equal-length `CharUpperBuff` comparison; locale collation is not substituted.
ANSI paths stay ANSI and Unicode builds use UTF-16. Neither movie strings nor
stored text encodings are converted by the backend.

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
app-data-root discovery live separately in the SDK-free `platform_users.h`;
their Windows implementation contains the WNet and shell-folder calls.

Temporary files now use exclusive creation after choosing a name. A collision
fails rather than truncating a file another process created in the meantime.
Ordinary `PfilCreate` retains its overwrite behavior. Temporary-name selection
still uses the existing bounded search and is not a new reservation API.

## POSIX backend

`platform_files_posix.cpp` supplies macOS/Linux file descriptors, native byte
paths, lexical absolute-path normalization, directory iteration and filesystem
operations. It handles interrupted read/write calls and complete transfers.
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
checks exercise the backend through the real core libraries. No SDL3 or ImGui
dependency is introduced; future host/UI adapters consume the same boundary.
