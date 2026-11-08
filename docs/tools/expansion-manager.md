# Expansion manager

The SDL port has an **Options > Expansions** tab for Nickelodeon, Doraemon and
Frankie's Expansion. Each installed pack has an enable checkbox. **OK** saves
changes in `settings.ini`; they take effect on the next launch, after open
content archives have closed. **Cancel** discards changes and newly staged
installations.

## Install

Choose the installation source with **Folder...** or **ISO...**, then click
**Install** on the appropriate row:

- **Prepared folder:** choose the directory containing the pack's `.3CN` and
  `.3TH` files. All three packs support this. Only recognized content files are
  copied and validated. Installers are never executed.
- **Doraemon ISO:** the eight original `CONTENTS` archives are extracted without
  modification.
- **Nickelodeon ISO:** also choose the original base 3DMM ISO and a local
  `3dmm-nick-image-fixups-1` ZIP. The base ISO field is filled automatically when
  the session was launched from a base ISO. Conversion validates input hashes,
  chunk IDs, links, flags, names, non-image data and all saved image pixels.
  Approximate palette conversion alone is not accepted. See
  [Nickelodeon recreation](nick-expansion.md) for creating local fixups.

Large imports can take a while in Debug builds. Choose **OK**, then close and
reopen the app to use the content. Original folders and ISOs remain untouched.

## Disable or uninstall

Uncheck a pack to keep its data but hide it from browsers on the next launch.
The resolver also blocks movie source references to disabled packs. A movie
requiring one reports that it must be enabled in Options and the app restarted.

**Remove** queues an existing managed installation for removal on the next
launch. Click it again to undo the choice, or use **Cancel** to discard the
dialog's changes. Remove immediately discards a newly staged installation.

Removal deletes only unchanged files listed in the installation's ownership
manifest. Changed content, unknown files, original sources, movies and external
installations are preserved. Locked files leave removal pending for a later
launch. Kept files and failures are reported in the runtime log.

Existing pack folders under the current content root appear as **external**.
They can be toggled but cannot be removed through the port. A managed pack
takes precedence over the matching external folder. Legacy add-ons sharing the
`Expansions` source are toggled together. Source IDs and movie references are
retained.

## Storage

The store is `content/packs` beside `settings.ini`, separate from the disposable
ISO cache. On Windows this is normally:

```
%APPDATA%\3DMMForever\Studio\content\packs
```

`THREEDMM_PACK_DIR` can specify an absolute alternative. Every installation has
a unique generation directory and a `pack.json` ownership manifest containing
file names, sizes and SHA-256 hashes. Imports use private staging directories;
only successful publication and preference saving select an installation.
Cancel rolls back this session's new files. Unselected directories left after
an interrupted process are preserved, since another instance might use them.

The `[User]` `ExpansionPacks` INI string contains JSON with each generation,
source ID, enable flag and removal flag, using the existing INI string escaping.
No registry data is written. Windows legacy registrations are read only for
source names and IDs.

The importer uses the existing chunky-file and expansion libraries, the ISO
reader, pinned miniz for ZIP reading and nlohmann/json for manifests. Python,
game artwork and local image residuals are not bundled into the executable.

## Validation

`studio-packs-test` covers SHA-256 vectors, staging, publication/rollback,
invalid inputs and ownership-preserving removal. Optional `--nick` and `--dora`
modes validate local discs against local reference content.
`tests/studio_packs_workflow.py` uses SDL dummy drivers and private settings to
exercise install/cancel, save/restart, toggles, browser discovery and removal
through the actual Options controls.
