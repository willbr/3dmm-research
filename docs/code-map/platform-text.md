# Text, comparison and native type boundaries

`platform_text.h` provides counted text conversion and comparison without
including the Windows SDK, application framework or UI libraries. `utilstr.cpp`
uses it for the existing String readers, user comparisons and case helpers.
File paths remain a separate concern in `platform_files.h`.

## Stored text compatibility

The existing encoding markers are unchanged: Mac single-byte `0x0202`, Windows
single-byte `0x0303`, big-endian UTF-16 `0x0404`, little-endian UTF-16 `0x0505`.
There is no new file encoding, version, normalization pass or save-format change.

- Mac-to-Windows single-byte translation and its reverse retain the original
  intentionally lossy tables in `chtrans.h`. Tests pin every table entry. This
  is distinct from decoding Mac Roman to Unicode.
- On Windows, conversions retain `CP_ACP` and `CP_MACCP` and native substitution/
  best-fit behavior. UTF-8 ACP uses the supported conversion flags. Existing
  ANSI application strings are not silently changed to UTF-8.
- Portable conversion uses explicit CP1252 and Mac Roman maps. UTF-16 input is
  decoded by byte order into 16-bit units; it does not depend on `wchar_t` width.
  Converting non-CP1252 Unicode to portable ANSI substitutes `?`; a valid
  supplementary-character pair produces one substitution. Conversion does not
  normalize or otherwise rewrite retained UTF-16 units.
- Input buffers are never modified, including big-endian input. Null output
  queries the required count. Insufficient capacity fails before touching output.
  Counted conversions preserve embedded nulls; String's stored-string reader
  continues to reject embedded nulls and missing terminators.

The Windows single-byte file marker does not record the actual Windows ANSI
code page. Consequently the portable CP1252 policy cannot establish equivalence
for every historical DBCS/non-Western file. Original encoded rich-text bytes
remain owned by `RichTextData`. A future UTF-8 SDL3/ImGui display adapter should
use an explicit conversion policy while preserving the archive's stored bytes.

## Comparison contracts

Internal byte-wise comparison remains separate from user comparison. Windows
user collation still uses `CompareStringA/W` with the existing locale/flags.
Equal-length case-insensitive equality still uppercases blocks before comparing
their bytes; locale collation has not replaced that equality contract.

`ChsUpper`/`ChsLower` retain the legacy cached per-byte map, with thread-safe
initialization. Mapping a full byte string remains a separate operation because
Windows DBCS behavior can depend on adjacent bytes.

The portable backend uses deterministic CP1252 case mapping and comparison.
UTF-16 case mapping covers that repertoire and leaves other units unchanged;
it is not complete Unicode linguistic collation or multi-character case folding.
This explicit fallback does not claim to duplicate every Windows user locale.
Regression dumps continue to sort numeric chunk identities, independently of
user collation.

Two inherited issues are corrected: the ANSI build's `UpperRgchw/LowerRgchw`
processed part of a UTF-16 buffer as ANSI bytes; they now map actual 16-bit units.
Rejected stored strings now clear their intermediate state directly rather than
asserting on an invalid terminator while trying to clear it. A failed nonempty
conversion is reported as a read failure instead of becoming an empty string.

## Public native interop

`util.h`, `kauai_core.h`, `bren_core.h` and `soc_core.h` no longer import Windows
SDK headers. Core declarations use `CoreNative` opaque tokens for native window,
region, picture and memory interoperability. Windows rectangle/point interop
types are forward-declared; ordinary core geometry continues to use `RC`/`PT`.
These native tokens are not persisted and their Windows ABI remains unchanged.

Native/UI implementations explicitly include `win_headers.h` or `frame.h`.
Caret timing lives in the GUI runtime. Removable-media error-dialog suppression
is hidden behind the file backend. Host assertion reporting still controls when
debugging is requested, with the debugger trap implemented in the platform layer.

Core link tests reject an accidental SDK import. The compile-only native interop
check includes core headers first, then the SDK, and checks handle and geometry
type compatibility. Windows x86/x64 consumers therefore exercise both include
orders without replacement GUI objects.

Native GDI region/metafile implementations now live in the GUI library, and
the LP64 core build uses explicit 32-bit legacy words. Core libraries and archive
tools build for macOS; the application still needs a portable window/device/UI
host. See [portable build and validation](library-split.md#platform-and-ui-direction).

## Checks

`library-checks` includes native text-service tests, the portable text backend
forced on Windows, and stored-string fixtures. Checks cover both UTF-16 byte
orders, counted nulls, short buffers, malformed data, readonly source buffers,
legacy mapping hashes, concurrent first-use mapping and native API comparisons.
The `.3mm` dump baseline and existing movie/actor tests remain separate gates.

The standalone `tests/platform` project also runs native and portable text
checks, including the Windows UTF-16 build and Linux/macOS CI jobs. Running the
portable text implementation on Windows gives local coverage without requiring
a working full non-Windows engine.
