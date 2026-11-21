/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#ifndef MOVIE_DATA_H
#define MOVIE_DATA_H
#include "kauai_core.h"
#include "tagman.h"
#include <cstdint>
#include "mm/movie_data.h"
#include "mm/movie_owner.h"
#include "mm/rollcall_storage.h"
#include "mm/movie_root.h"

struct MovieFilePrefix
{
    int16_t bo;       // byte order
    int16_t osk;      // which system wrote this
    DataVersion dver; // chunky file version
};
static_assert(sizeof(MovieFilePrefix) == 8, "MovieFilePrefix on-disk layout drift");
const ByteOrderMask kbomMfp = 0x55000000;

// Fixed-width GST actor row used by the compiler and format fixtures.
struct RollCallActorEntryOnFile
{
    int32_t arid;
    int32_t cactRef;
    uint32_t grfbrws;
    TAGOnFile tagTmpl;
};
static_assert(sizeof(RollCallActorEntryOnFile) == 28, "RollCallActorEntryOnFile wire format drift");

// State shared by document/view and headless persistence hosts. The actor
// table owns opened TAG references; resources is an owned reference as well.
struct MovieData : MMMovieState
{
    MovieData() { mm_movie_state_init(this); mm_movie_title_init(&title, sizeof(achar)); }
    void GetTitle(PString) const;
    void SetTitle(PString);
    PChunkyResourceFile Resources() const { return static_cast<PChunkyResourceFile>(resources); }
    ~MovieData();
    MovieData(const MovieData &) = delete;
    MovieData &operator=(const MovieData &) = delete;
    void ClearActors();
    bool FInitActors();
    bool FInitEmpty();
    bool FLoad(PFilename filename, ChunkNumber number = cnoNil);
    // Saves the chunk graph, including opaque scene/asset children. Runtime
    // scene encoding and garbage collection remain the caller's responsibility.
    bool FSave(PFilename filename);

    static bool FCreateRoot(PChunkyFile file, ChunkNumber *number);
    static bool FVerifyVersion(MMChunkOwnerRead *, ChunkNumber *number = pvNil, MMMovieHeader *header = pvNil);
    static bool FLoadRollCall(PChunkyResourceFile resources, ChunkNumber number, MMRollCallTable *actors,
                              MMI32 *limit = nullptr);
    static bool FWriteRollCall(PChunkyFile file, ChunkNumber number, MMRollCallTable *actors, ChunkNumber *written);
  private:
    friend class Movie;
    static MMMovieSessionBinding _SessionBinding();
    static MMRollCallBinding _RollCallBinding();
    static MMMovieOwnerServices _OwnerServices(void *);
};
#endif
