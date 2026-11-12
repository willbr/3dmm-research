/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef MOVIE_DATA_H
#define MOVIE_DATA_H
#include "kauai_core.h"
#include "tagman.h"
#include <cstdint>
#include "three_mm/movie_data.h"
#include "three_mm/movie_owner.h"
#include "three_mm/rollcall_storage.h"
#include "three_mm/movie_root.h"

struct MovieFilePrefix
{
    int16_t bo;       // byte order
    int16_t osk;      // which system wrote this
    DataVersion dver; // chunky file version
};
static_assert(sizeof(MovieFilePrefix) == 8, "MovieFilePrefix on-disk layout drift");
const ByteOrderMask kbomMfp = 0x55000000;

//
// Used to keep track of the roll call list of the movie.
// Runtime form: embeds a full TAG (with runtime pcrf). Size is allowed to
// grow on x64 — this struct never reaches disk; the GST is marshalled to/from
// RollCallActorEntryOnFile at the I/O boundary (see _ReadRollCallExtra and
// the save path).
//
struct RollCallActorEntry
{
    int32_t arid;
    int32_t cactRef;
    uint32_t grfbrws; // browser properties
    TAG tagTmpl;
};

typedef RollCallActorEntry *PRollCallActorEntry;

// Wire format for RollCallActorEntry. Fixed at 28 bytes on every architecture
// because TAGOnFile is fixed-width. This is what lands in the GST on disk.
struct RollCallActorEntryOnFile
{
    int32_t arid;
    int32_t cactRef;
    uint32_t grfbrws;
    TAGOnFile tagTmpl;
};
static_assert(sizeof(RollCallActorEntryOnFile) == 28, "RollCallActorEntryOnFile wire format drift");

const ByteOrderMask kbomMactr = 0xfffc0000;

// Temporary native adapter over C-owned roll-call storage. Table destruction
// releases storage only; movie transactions own the opened tag references.
class RollCallTable : public BASE
{
    MARKMEM
    ASSERT
    static RollCallTable *_New(ThreeMMU32 extra);
  public:
    ThreeMMRollCallTable storage;
    static RollCallTable *New(bool wire = false);
    static RollCallTable *Read(PDataBlock block, int *bigEndian);
    ~RollCallTable();
    LegacyLong IvMac() const { return storage.count; }
    LegacyLong CbExtra() const { return storage.stride - 4; }
    LegacyLong CbOnFile() const { return three_mm_rollcall_table_wire_size(&storage); }
    void GetStn(LegacyLong index, PString name);
    void GetExtra(LegacyLong index, RollCallActorEntry *actor);
    void PutExtra(LegacyLong index, const RollCallActorEntry *actor);
    bool FAddStn(PString name, const RollCallActorEntry *actor);
    bool FPutStn(LegacyLong index, PString name);
    void Delete(LegacyLong index);
    bool FWrite(PDataBlock);
};
typedef RollCallTable *PRollCallTable;

// State shared by document/view and headless persistence hosts. The actor
// table owns opened TAG references; resources is an owned reference as well.
struct MovieData : ThreeMMMovieState
{
    MovieData() { three_mm_movie_state_init(this); three_mm_movie_title_init(&title, sizeof(achar)); }
    void GetTitle(PString) const;
    void SetTitle(PString);
    bool FWriteTitle(PChunkyFile);
    PChunkyResourceFile Resources() const { return static_cast<PChunkyResourceFile>(resources); }
    PRollCallTable Actors() const { return static_cast<PRollCallTable>(actors); }
    void ReleaseResources();
    ~MovieData();
    MovieData(const MovieData &) = delete;
    MovieData &operator=(const MovieData &) = delete;
    void ClearActors();
    bool FInitEmpty();
    bool FLoad(PFilename filename, ChunkNumber number = cnoNil);
    // Saves the chunk graph, including opaque scene/asset children. Runtime
    // scene encoding and garbage collection remain the caller's responsibility.
    bool FSave(PFilename filename);

    static bool FCreateRoot(PChunkyFile file, ChunkNumber *number);
    static bool FVerifyVersion(PChunkyFile file, ChunkNumber *number = pvNil, ThreeMMMovieHeader *header = pvNil);
    static bool FReadRollCall(PChunkyResourceFile resources, ChunkNumber number, PRollCallTable *actors,
                              LegacyLong *actorIdLimit = pvNil);
    static bool FWriteRollCall(PChunkyFile file, ChunkNumber number, PRollCallTable actors, ChunkNumber *written);
  private:
    static ThreeMMMovieOwnerServices _OwnerServices(void *);
};
#endif
