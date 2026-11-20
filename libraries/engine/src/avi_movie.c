/* AVI and Video 1 decoding converted from Kauai avi_movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/avi_movie.h"
#include "mm/audio_clock.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

/* Format references for the original independent decoder:
 * https://multimedia.cx/video1.txt
 * https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference */

#define AVI_NO_STREAM ((MMU32)-1)
#define AVI_MAX_FILE (256UL * 1024 * 1024)
#define AVI_MAX_FRAMES (256UL * 1024)
#define AVI_MAX_PIXELS (16UL * 1024 * 1024)

typedef struct AviChunk
{
    const unsigned char *tag;
    MMU32 offset, bytes;
} AviChunk;

typedef struct AviChunks
{
    const unsigned char *file;
    MMU32 cursor, end;
} AviChunks;

typedef struct AviFrame
{
    MMU32 offset, bytes;
    unsigned char palette[256][4];
} AviFrame;

struct MMAviMovie
{
    MMAviAllocator allocator;
    MMAviInfo info;
    MMAviAudio audio;
    unsigned char *file, *pixels;
    AviFrame *frames;
    MMI32 decoded;
};

typedef struct AviDemux
{
    MMAviMovie *movie;
    MMU32 video, audio, next_frame, pcm_bytes;
    unsigned char palette[256][4];
    unsigned char *pcm;
} AviDemux;

static unsigned u16(const unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

static MMU32 u32(const unsigned char *p)
{
    return u16(p) | ((MMU32)u16(p + 2) << 16);
}

static int tag(const unsigned char *p, const char *fourcc)
{
    return memcmp(p, fourcc, 4) == 0;
}

static int video1(const unsigned char *p)
{
    unsigned char upper[4];
    unsigned i;
    for (i = 0; i < 4; ++i)
        upper[i] = (unsigned char)(p[i] >= 'a' && p[i] <= 'z' ? p[i] - 32 : p[i]);
    return tag(upper, "MSVC") || tag(upper, "CRAM") || tag(upper, "WHAM");
}

static int dimensions(MMI32 width, MMI32 height)
{
    return width > 0 && height > 0 && !(width % 4) && !(height % 4) && (MMU32)height <= AVI_MAX_PIXELS / width;
}

static AviChunks subchunks(const unsigned char *file, const AviChunk *list)
{
    AviChunks result;
    result.file = file;
    result.cursor = list->offset + 4;
    result.end = list->offset + list->bytes;
    return result;
}

/* Check each chunk and its WORD padding against the enclosing list. */
static int next_chunk(AviChunks *chunks, AviChunk *chunk)
{
    MMU32 available, padded;
    if (chunks->cursor == chunks->end)
        return 0;
    if (chunks->end - chunks->cursor < 8)
        return -1;
    chunk->tag = chunks->file + chunks->cursor;
    chunk->offset = chunks->cursor + 8;
    chunk->bytes = u32(chunk->tag + 4);
    available = chunks->end - chunk->offset;
    if (chunk->bytes > available)
        return -1;
    padded = chunk->bytes + (chunk->bytes & 1);
    if (padded > available)
        return -1;
    chunks->cursor = chunk->offset + padded;
    return 1;
}

/* With no destination this checks every packet boundary before applying a
 * delta to the owned reference frame. */
static int decode_packet(const unsigned char *p, size_t bytes, MMI32 w, MMI32 h, unsigned char *pixels)
{
    const MMU32 across = (MMU32)w / 4, blocks = across * ((MMU32)h / 4);
    MMU32 block = 0, skip, left, bottom;
    unsigned a, b, colors, flags, x, y, pair, index;
    size_t pos = 0;
    while (block < blocks)
    {
        if (bytes - pos < 2)
            return 0;
        a = p[pos++];
        b = p[pos++];
        if (b >= 0x84 && b < 0x88)
        {
            skip = (b - 0x84) * 256 + a;
            if (!skip || skip > blocks - block)
                return 0;
            block += skip;
            continue;
        }
        colors = b < 0x80 ? 2 : b >= 0x90 ? 8 : 0;
        if (colors > bytes - pos)
            return 0;
        if (pixels)
        {
            flags = a | (b << 8);
            left = (block % across) * 4;
            bottom = (MMU32)h - 1 - block / across * 4;
            for (y = 0; y < 4; ++y)
                for (x = 0; x < 4; ++x)
                {
                    pair = colors == 8 ? (y / 2 * 2 + x / 2) * 2 : 0;
                    index = colors ? p[pos + pair + ((flags >> (y * 4 + x)) & 1 ? 0 : 1)] : a;
                    pixels[(size_t)(bottom - y) * w + left + x] = (unsigned char)index;
                }
        }
        pos += colors;
        ++block;
    }
    /* Some encoders omit the optional final 0000 marker. */
    return pos == bytes || (bytes - pos == 2 && p[pos] == 0 && p[pos + 1] == 0);
}

int mm_avi_decode_video1(const void *packet, size_t bytes, MMI32 w, MMI32 h,
                         unsigned char *pixels, size_t pixel_count)
{
    unsigned char *next;
    int valid;
    if (!packet || !pixels || !dimensions(w, h) || pixel_count != (MMU32)w * (MMU32)h)
        return 0;
    next = (unsigned char *)malloc(pixel_count);
    if (!next)
        return 0;
    memcpy(next, pixels, pixel_count);
    valid = decode_packet((const unsigned char *)packet, bytes, w, h, next);
    if (valid)
        memcpy(pixels, next, pixel_count);
    free(next);
    return valid;
}

static MMAudioCounter divide_by(MMAudioCounter value, MMU32 divisor)
{
    return mm_audio_count_divide(value, mm_audio_counter(divisor, 0), NULL);
}

static int video_header(AviDemux *demux, MMU32 stream, const unsigned char *sh, const AviChunk *format)
{
    MMAviInfo *info = &demux->movie->info;
    const unsigned char *sf = demux->movie->file + format->offset;
    MMAudioCounter samples, limit;
    MMU32 colors, i;
    if (demux->video != AVI_NO_STREAM || format->bytes < 40 || u32(sf) != 40 || !video1(sf + 16) ||
        u16(sf + 12) != 1 || u16(sf + 14) != 8)
        return 0;
    info->width = mm_signed_word(u32(sf + 4));
    info->height = mm_signed_word(u32(sf + 8));
    info->frames = mm_signed_word(u32(sh + 32));
    info->scale = u32(sh + 20);
    info->rate = u32(sh + 24);
    info->start = u32(sh + 28);
    if (!dimensions(info->width, info->height) || info->frames <= 0 || (MMU32)info->frames > AVI_MAX_FRAMES ||
        !info->scale || !info->rate || info->start > 0x7fffffffUL - (MMU32)info->frames)
        return 0;
    samples = mm_audio_count_multiply(info->start + (MMU32)info->frames, info->scale);
    limit = divide_by(mm_audio_count_multiply(0x7fffffffUL, info->rate), 1000);
    if (mm_audio_count_compare(samples, limit) > 0)
        return 0;
    colors = u32(sf + 32);
    if (!colors)
        colors = 256;
    if (colors > 256 || format->bytes < 40 + colors * 4)
        return 0;
    for (i = 0; i < colors; ++i)
        memcpy(demux->palette[i], sf + 40 + i * 4, 3);
    demux->video = stream;
    return 1;
}

static int audio_header(AviDemux *demux, MMU32 stream, const unsigned char *sh, const AviChunk *format)
{
    MMAviAudio *audio = &demux->movie->audio;
    const unsigned char *sf = demux->movie->file + format->offset;
    unsigned align;
    if (demux->audio != AVI_NO_STREAM || format->bytes < 16 || u16(sf) != 1)
        return 0;
    audio->channels = u16(sf + 2);
    audio->bits = u16(sf + 14);
    audio->rate = u32(sf + 4);
    if ((audio->channels != 1 && audio->channels != 2) || (audio->bits != 8 && audio->bits != 16) ||
        !audio->rate || audio->rate > 384000)
        return 0;
    align = audio->channels * (audio->bits / 8);
    if (u16(sf + 12) != align || u32(sf + 8) != audio->rate * align || u32(sh + 20) != align ||
        u32(sh + 24) != audio->rate * align || u32(sh + 44) != align)
        return 0;
    audio->start = u32(sh + 28);
    demux->audio = stream;
    return 1;
}

static int stream_header(AviDemux *demux, MMU32 index, const AviChunk *list)
{
    const unsigned char *file = demux->movie->file, *sh;
    AviChunk header = {0}, format = {0}, chunk;
    AviChunks chunks = subchunks(file, list);
    int status;
    while ((status = next_chunk(&chunks, &chunk)) > 0)
    {
        if (tag(chunk.tag, "strh"))
        {
            if (header.tag)
                return 0;
            header = chunk;
        }
        else if (tag(chunk.tag, "strf"))
        {
            if (format.tag)
                return 0;
            format = chunk;
        }
    }
    if (status < 0 || !header.tag || header.bytes < 48 || !format.tag)
        return 0;
    sh = file + header.offset;
    if (tag(sh, "vids"))
        return video_header(demux, index, sh, &format);
    if (tag(sh, "auds"))
        return audio_header(demux, index, sh, &format);
    return 1;
}

static int headers(AviDemux *demux, const AviChunk *list)
{
    const unsigned char *file = demux->movie->file;
    AviChunks chunks = subchunks(file, list);
    AviChunk chunk;
    MMU32 stream = 0;
    int status;
    while ((status = next_chunk(&chunks, &chunk)) > 0)
    {
        if (!tag(chunk.tag, "LIST"))
            continue;
        if (chunk.bytes < 4)
            return 0;
        if (tag(file + chunk.offset, "strl") &&
            (stream >= 100 || !stream_header(demux, stream++, &chunk)))
            return 0;
    }
    return status == 0 && demux->video != AVI_NO_STREAM;
}

static int video_packet(AviDemux *demux, const AviChunk *chunk)
{
    MMAviMovie *movie = demux->movie;
    const unsigned char *p = movie->file + chunk->offset;
    unsigned count, i, first;
    AviFrame *frame;
    if (chunk->tag[2] == 'p' && chunk->tag[3] == 'c')
    {
        if (chunk->bytes < 4)
            return 0;
        count = p[1] ? p[1] : 256;
        first = p[0];
        if (first + count > 256 || chunk->bytes != 4 + count * 4)
            return 0;
        if (movie->frames)
            for (i = 0, p += 4; i < count; ++i, p += 4)
            {
                /* PALETTEENTRY is RGB, unlike the header's RGBQUAD. */
                demux->palette[first + i][0] = p[2];
                demux->palette[first + i][1] = p[1];
                demux->palette[first + i][2] = p[0];
            }
        return 1;
    }
    if (chunk->tag[2] != 'd' || chunk->tag[3] != 'c' || demux->next_frame >= (MMU32)movie->info.frames)
        return 0;
    if (movie->frames)
    {
        frame = &movie->frames[demux->next_frame];
        frame->offset = chunk->offset;
        frame->bytes = chunk->bytes;
        memcpy(frame->palette, demux->palette, sizeof(frame->palette));
    }
    ++demux->next_frame;
    return 1;
}

static int movie_list(AviDemux *demux, const AviChunk *list, unsigned depth)
{
    const unsigned char *file = demux->movie->file, *id;
    const MMAviAudio *audio = &demux->movie->audio;
    AviChunks chunks = subchunks(file, list);
    AviChunk chunk;
    MMU32 stream;
    int status;
    if (depth > 8)
        return 0;
    while ((status = next_chunk(&chunks, &chunk)) > 0)
    {
        id = chunk.tag;
        if (tag(id, "LIST"))
        {
            if (chunk.bytes < 4 || !tag(file + chunk.offset, "rec ") || !movie_list(demux, &chunk, depth + 1))
                return 0;
            continue;
        }
        if (id[0] < '0' || id[0] > '9' || id[1] < '0' || id[1] > '9')
            continue;
        stream = (MMU32)(id[0] - '0') * 10 + (MMU32)(id[1] - '0');
        if (stream == demux->video)
        {
            if (!video_packet(demux, &chunk))
                return 0;
        }
        else if (stream == demux->audio)
        {
            if (id[2] != 'w' || id[3] != 'b' || chunk.bytes % (audio->channels * (audio->bits / 8)))
                return 0;
            if (demux->pcm && chunk.bytes)
                memcpy(demux->pcm + demux->pcm_bytes, file + chunk.offset, chunk.bytes);
            demux->pcm_bytes += chunk.bytes;
        }
    }
    return status == 0;
}

static void *allocate(const MMAviAllocator *allocator, size_t bytes)
{
    return allocator->allocate ? allocator->allocate(allocator->context, bytes) : malloc(bytes);
}

static void release(const MMAviAllocator *allocator, void *data)
{
    if (data)
    {
        if (allocator->release)
            allocator->release(allocator->context, data);
        else
            free(data);
    }
}

void mm_avi_close(MMAviMovie *movie)
{
    MMAviAllocator allocator;
    if (!movie)
        return;
    allocator = movie->allocator;
    release(&allocator, movie->pixels);
    release(&allocator, (void *)movie->audio.pcm);
    release(&allocator, movie->frames);
    release(&allocator, movie->file);
    release(&allocator, movie);
}

static int demux_movie(MMAviMovie *movie, MMU32 bytes)
{
    AviChunks chunks;
    AviChunk hdrl = {0}, movi = {0}, chunk;
    AviDemux demux = {0};
    int status;
    size_t pixels;
    chunks.file = movie->file;
    chunks.cursor = 12;
    chunks.end = bytes;
    while ((status = next_chunk(&chunks, &chunk)) > 0)
    {
        if (!tag(chunk.tag, "LIST"))
            continue;
        if (chunk.bytes < 4)
            return 0;
        if (tag(movie->file + chunk.offset, "hdrl"))
        {
            if (hdrl.tag || movi.tag)
                return 0;
            hdrl = chunk;
        }
        else if (tag(movie->file + chunk.offset, "movi"))
        {
            if (movi.tag || !hdrl.tag)
                return 0;
            movi = chunk;
        }
    }
    if (status < 0 || !hdrl.tag || !movi.tag)
        return 0;
    demux.movie = movie;
    demux.video = demux.audio = AVI_NO_STREAM;
    /* Count and validate chunk payloads before allocating the index and PCM.
     * The second walk reads our owned bytes and records exact palette states. */
    if (!headers(&demux, &hdrl) || !movie_list(&demux, &movi, 0) || demux.next_frame != (MMU32)movie->info.frames)
        return 0;
    if ((size_t)(MMU32)movie->info.frames > (size_t)-1 / sizeof(AviFrame))
        return 0;
    movie->frames = (AviFrame *)allocate(&movie->allocator, (size_t)movie->info.frames * sizeof(AviFrame));
    if (!movie->frames)
        return 0;
    if (demux.pcm_bytes)
    {
        demux.pcm = (unsigned char *)allocate(&movie->allocator, demux.pcm_bytes);
        if (!demux.pcm)
            return 0;
        movie->audio.pcm = demux.pcm;
        movie->audio.pcm_bytes = demux.pcm_bytes;
    }
    pixels = (size_t)movie->info.width * movie->info.height;
    movie->pixels = (unsigned char *)allocate(&movie->allocator, pixels);
    if (!movie->pixels)
        return 0;
    memset(movie->pixels, 0, pixels);
    movie->decoded = -1;
    demux.next_frame = demux.pcm_bytes = 0;
    return movie_list(&demux, &movi, 0);
}

int mm_avi_read(MMAviMovie **slot, const void *source, size_t bytes, const MMAviAllocator *allocator)
{
    const unsigned char *p = (const unsigned char *)source;
    MMAviAllocator storage = {0};
    MMAviMovie *next;
    if (!slot || !p || bytes < 12 || bytes > AVI_MAX_FILE ||
        !tag(p, "RIFF") || !tag(p + 8, "AVI ") || u32(p + 4) != bytes - 8)
        return 0;
    if (allocator)
    {
        if (!allocator->allocate || !allocator->release)
            return 0;
        storage = *allocator;
    }
    next = (MMAviMovie *)allocate(&storage, sizeof(*next));
    if (!next)
        return 0;
    memset(next, 0, sizeof(*next));
    next->allocator = storage;
    next->file = (unsigned char *)allocate(&storage, bytes);
    if (next->file)
    {
        memcpy(next->file, p, bytes);
        if (demux_movie(next, (MMU32)bytes))
        {
            mm_avi_close(*slot);
            *slot = next;
            return 1;
        }
    }
    mm_avi_close(next);
    return 0;
}

MMAviInfo mm_avi_info(const MMAviMovie *movie)
{
    MMAviInfo empty = {0};
    return movie ? movie->info : empty;
}

MMAviAudio mm_avi_audio(const MMAviMovie *movie)
{
    MMAviAudio empty = {0};
    return movie ? movie->audio : empty;
}

MMI32 mm_avi_time_for_frame(const MMAviMovie *movie, MMI32 frame)
{
    MMAudioCounter samples, milliseconds;
    if (!movie || frame < 0 || frame > movie->info.frames)
        return -1;
    samples = mm_audio_count_multiply((MMU32)frame + movie->info.start, movie->info.scale);
    milliseconds = mm_audio_count_multiply(samples.low, 1000);
    milliseconds.high += samples.high * 1000;
    return (MMI32)divide_by(milliseconds, movie->info.rate).low;
}

MMI32 mm_avi_frame_for_time(const MMAviMovie *movie, MMI32 milliseconds)
{
    const MMAviInfo *info;
    MMAudioCounter samples;
    if (!movie)
        return -1;
    info = &movie->info;
    if (milliseconds < 0)
        return -1 - (MMI32)info->start;
    samples = mm_audio_count_divide(mm_audio_count_multiply((MMU32)milliseconds, info->rate),
                                    mm_audio_count_multiply(info->scale, 1000), NULL);
    if (samples.high || samples.low >= info->start + (MMU32)info->frames)
        return info->frames;
    return (MMI32)samples.low - (MMI32)info->start;
}

size_t mm_avi_frame_bytes(const MMAviMovie *movie, MMI32 frame)
{
    if (!movie || frame < 0 || frame >= movie->info.frames)
        return 0;
    return (size_t)movie->info.width * movie->info.height * 4;
}

int mm_avi_decode_frame(MMAviMovie *movie, MMI32 frame, unsigned char *output, size_t capacity)
{
    const AviFrame *packet;
    const unsigned char *source;
    size_t required = mm_avi_frame_bytes(movie, frame), i;
    MMI32 w, h;
    if (!required || !output || capacity < required)
        return 0;
    w = movie->info.width;
    h = movie->info.height;
    if (frame < movie->decoded)
    {
        memset(movie->pixels, 0, required / 4);
        movie->decoded = -1;
    }
    while (movie->decoded < frame)
    {
        packet = &movie->frames[movie->decoded + 1];
        source = movie->file + packet->offset;
        /* Empty packets repeat the preceding indexed frame. */
        if (packet->bytes)
        {
            if (!decode_packet(source, packet->bytes, w, h, NULL))
                return 0;
            decode_packet(source, packet->bytes, w, h, movie->pixels);
        }
        ++movie->decoded;
    }
    packet = &movie->frames[frame];
    for (i = 0; i < required / 4; ++i)
        memcpy(output + i * 4, packet->palette[movie->pixels[i]], 4);
    return 1;
}
