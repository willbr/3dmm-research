/* Plain filename enumeration frames converted from Kauai filename.cpp/fni.h.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILENAME_FRAME_H
#define MM_ENGINE_FILENAME_FRAME_H
#include "filename_enum.h"
#include "filename_owned.h"
#include "reference.h"
#include "object_debug.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMFilenameFrameMetadata {
    MMReference references;
    MMObjectDebugState debug;
} MMFilenameFrameMetadata;
typedef struct MMFilenameMetadataView {
    MMReference *references;
    MMObjectDebugState *debug;
} MMFilenameMetadataView;
typedef struct MMFilenameFrame8 {
    MMFilenameOwned8 name;
    MMFilenameFrameMetadata metadata;
    MMFilenameEnumCursor cursor;
    unsigned char entry[1024];
} MMFilenameFrame8;
typedef struct MMFilenameFrame16 {
    MMFilenameOwned16 name;
    MMFilenameFrameMetadata metadata;
    MMFilenameEnumCursor cursor;
    unsigned short entry[1024];
} MMFilenameFrame16;
/* Plain C recursion snapshots, with no vtable, self pointers or C++ lifetime.
 * Init once before enum construction; it sets nil name, local/count-one metadata,
 * zero cursor/entry and leaves the unused counted-path tail untouched. Views
 * borrow the record and use NULL name.owner, distinguishing a C value from a
 * native object. Rebuild views after relocation; enum pop restores the same
 * fixed record address. Use sizeof(the selected profile) for frame_bytes.
 * Active iterator ownership follows filename_enum.h: raw snapshots transfer
 * suspended cursors, and every live iterator is closed once by the controller.
 * Metadata only preserves legacy value-copy payload; it owns no references or
 * debug-graph registration. Copying it does not retain/drop an object or create
 * a polymorphic object. Native bindings supply their existing metadata fields;
 * NULL fields skip that field. Copy publishes metadata, then type/path using
 * filename_owned.h assignment diagnostics and prefix-only transfer. Units match,
 * views remain live, and callbacks follow that header's restrictions. */
void mm_filename_frame8_init(MMFilenameFrame8 *);
void mm_filename_frame16_init(MMFilenameFrame16 *);
MMFilenameEnumFrame mm_filename_frame8_view(MMFilenameFrame8 *);
MMFilenameEnumFrame mm_filename_frame16_view(MMFilenameFrame16 *);
MMFilenameMetadataView mm_filename_frame_metadata(MMFilenameFrameMetadata *);
void mm_filename_frame_copy_name(const MMFilenameValue *, const MMFilenameMetadataView *,
                                 const MMFilenameValue *, const MMFilenameMetadataView *,
                                 const MMFilenameOwnedHost *);
#ifdef __cplusplus
}
#endif
#endif
