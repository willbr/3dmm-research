/* MIDI policy converted from Kauai midi.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/midi.h"
#include <string.h>
static int variable(const ThreeMMMidiParser *p, ThreeMMU32 *at, ThreeMMU32 *value)
{
    unsigned char b;
    *value = 0;
    do
    {
        if (*at >= p->size)
            return 0;
        b = p->data[(*at)++];
        *value = (*value << 7) + (b & 127);
    } while (b & 128);
    return 1;
}
void three_mm_midi_parser_init(ThreeMMMidiParser *p, const void *data, ThreeMMU32 size, ThreeMMU32 start,
                               ThreeMMI32 tempo)
{
    memset(p, 0, sizeof(*p));
    p->data = (const unsigned char *)data;
    p->size = data ? size : 0;
    p->timestamp = start;
    p->tempo = tempo;
}
int three_mm_midi_next(ThreeMMMidiParser *p, ThreeMMMidiEvent *out, int advance)
{
    ThreeMMMidiEvent e;
    ThreeMMU32 at = p->cursor, time = p->timestamp, n, delta;
    unsigned char b;
    int count;
    memset(&e, 0, sizeof(e));
    e.tempo = p->tempo;
    if (!p->data)
        goto fail;
    while (!e.count)
    {
        if (!variable(p, &at, &delta) || at >= p->size)
            goto fail;
        time += delta;
        e.timestamp = time;
        b = p->data[at];
        if (b & 128)
        {
            p->status = b;
            ++at;
        }
        e.send[0] = p->status;
        count = 0;
        switch (p->status & 240)
        {
        case 0x80:
        case 0x90:
        case 0xA0:
        case 0xB0:
        case 0xE0:
            count = 2;
            break;
        case 0xC0:
        case 0xD0:
            count = 1;
            break;
        case 0xF0:
            switch (p->status & 15)
            {
            case 2:
                count = 2;
                break;
            case 1:
            case 3:
                count = 1;
                break;
            case 6:
            case 8:
            case 10:
            case 11:
            case 12:
            case 14:
                p->status = 0;
                e.count = 1;
                break;
            case 0:
            case 7:
                if (!variable(p, &at, &n) || !n || n > p->size - at)
                    goto fail;
                at += n;
                break;
            case 15:
                if (at >= p->size)
                    goto fail;
                b = p->data[at++];
                if (!variable(p, &at, &n) || n > p->size - at)
                    goto fail;
                if (b == 0x2F)
                    goto fail;
                if (b == 0x51)
                {
                    if (n != 3)
                        goto fail;
                    e.tempo = ((ThreeMMI32)p->data[at] << 16) | ((ThreeMMI32)p->data[at + 1] << 8) | p->data[at + 2];
                    if (advance)
                        p->tempo = e.tempo;
                    at += 3;
                    goto send;
                }
                at += n;
                break;
            default:
                goto fail;
            }
            break;
        default:
            goto fail;
        }
        if (count)
        {
            if ((ThreeMMU32)count > p->size - at)
                goto fail;
            memcpy(e.send + 1, p->data + at, (size_t)count);
            at += (unsigned)count;
            e.count = count + 1;
        }
    }
send:
    if (out)
        *out = e;
    if (advance)
    {
        p->cursor = at;
        p->timestamp = time;
    }
    return 1;
fail:
    p->cursor = p->size;
    return 0;
}
unsigned three_mm_midi_encode_time(ThreeMMU32 n, unsigned char *out)
{
    unsigned char bytes[5];
    unsigned count = 0, i;
    bytes[count++] = (unsigned char)(n & 127);
    while ((n >>= 7) != 0)
        bytes[count++] = (unsigned char)((n & 127) | 128);
    if (out)
        for (i = 0; i < count; ++i)
            out[i] = bytes[count - i - 1];
    return count;
}
typedef struct Track
{
    ThreeMMMidiParser parser;
    ThreeMMMidiEvent event;
} Track;
static unsigned be16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}
static ThreeMMU32 be32(const unsigned char *p)
{
    return ((ThreeMMU32)be16(p) << 16) | be16(p + 2);
}
static ThreeMMU32 gcd(ThreeMMU32 a, ThreeMMU32 b)
{
    ThreeMMU32 r;
    while (b)
    {
        r = a % b;
        a = b;
        b = r;
    }
    return a;
}
/* Exact signed 32x32/32 using two words, with truncation toward zero. */
static int scale(ThreeMMU32 raw, ThreeMMU32 num, ThreeMMU32 den, ThreeMMU32 *out)
{
    ThreeMMU32 a = raw, lo, hi, cross, x, y, rem = 0, q = 0, bit, next;
    int negative = (raw & 0x80000000UL) != 0, i;
    if (negative)
        a = 0U - a;
    x = (a & 65535) * (num & 65535);
    y = (a >> 16) * (num & 65535) + (x >> 16);
    cross = (a & 65535) * (num >> 16) + (y & 65535);
    lo = (x & 65535) | (cross << 16);
    hi = (a >> 16) * (num >> 16) + (y >> 16) + (cross >> 16);
    for (i = 63; i >= 0; --i)
    {
        bit = i >= 32 ? ((hi >> (i - 32)) & 1) : ((lo >> i) & 1);
        next = (rem << 1) | bit;
        if ((rem & 0x80000000UL) || next >= den)
        {
            next -= den;
            if (i >= 32)
                return 0;
            q |= (ThreeMMU32)1 << i;
        }
        rem = next;
    }
    if (q > (negative ? 0x80000000UL : 0x7fffffffUL))
        return 0;
    *out = negative ? 0U - q : q;
    return 1;
}
static int append(ThreeMMCollectionStorage *s, ThreeMMU32 *size, const void *p, ThreeMMU32 n)
{
    if (n >= THREE_MM_STORAGE_LIMIT - *size ||
        three_mm_storage_ensure(s, *size + n, s->buffers[1].capacity, 0) != THREE_MM_STORAGE_OK)
        return 0;
    memcpy(s->buffers[0].data + *size, p, n);
    *size += n;
    return 1;
}
int three_mm_midi_import(const void *data, ThreeMMU32 bytes, ThreeMMCollectionStorage *s, ThreeMMU32 *length)
{
    const unsigned char *p = (const unsigned char *)data;
    ThreeMMU32 at, n, tracks = 0, index, minimum, tsmin, ts, rawtempo = 0, tstempo = 0, last = 0, size = 0;
    ThreeMMU32 num, den, newnum, newden, g, t;
    unsigned format, division, fps, ticks;
    int smpte, sequential;
    Track track, *list;
    unsigned char encoded[5];
    if (!p || !s || !length || bytes < 14 || memcmp(p, "MThd", 4))
        return 0;
    n = be32(p + 4);
    /* Legacy requires at least one byte beyond the file header. */
    if (n < 6 || n >= bytes - 8)
        return 0;
    format = be16(p + 8);
    division = be16(p + 12);
    at = 8 + n;
    while (at < bytes)
    {
        if (bytes - at < 8)
            return 0;
        n = be32(p + at + 4);
        at += 8;
        if (n > bytes - at)
            return 0;
        if (!memcmp(p + at - 8, "MTrk", 4) && n)
        {
            three_mm_midi_parser_init(&track.parser, p + at, n, 0, 500000);
            if (three_mm_midi_next(&track.parser, &track.event, 1))
            {
                if (tracks >= THREE_MM_STORAGE_LIMIT / sizeof(Track) - 1 ||
                    three_mm_storage_ensure(s, s->buffers[0].capacity, (tracks + 1) * (ThreeMMU32)sizeof(Track), 0) !=
                        THREE_MM_STORAGE_OK)
                    return 0;
                memcpy(s->buffers[1].data + tracks * sizeof(Track), &track, sizeof(track));
                ++tracks;
                if (format != 1 && format != 2)
                    break;
            }
        }
        at += n;
    }
    if (!tracks)
        return 0;
    smpte = (division & 0x8000) != 0;
    if (smpte)
    {
        fps = (256 - (division >> 8)) & 255;
        ticks = division & 255;
        if (fps == 29)
            fps = 30;
        if (!ticks || !fps)
            return 0;
        num = 1000;
        den = fps * ticks;
    }
    else
    {
        if (!division)
            return 0;
        num = 500;
        den = division;
    }
    g = gcd(num, den);
    num /= g;
    den /= g;
    sequential = tracks == 1 || format == 2;
    s->min_grow[0] = 1024; /* Match the native import stream growth policy. */
    list = (Track *)s->buffers[1].data;
    for (;;)
    {
        minimum = 0;
        if (!sequential)
        {
            tsmin = 0xffffffffUL;
            for (index = 0; index < tracks; ++index)
                if (list[index].event.timestamp < tsmin)
                {
                    minimum = index;
                    tsmin = list[index].event.timestamp;
                }
            if (tsmin == 0xffffffffUL)
                return 0;
        }
        track = list[minimum];
        if (!scale(track.event.timestamp - rawtempo, num, den, &t))
            return 0;
        ts = t + tstempo;
        if (!track.event.count)
        {
            if (!smpte)
            {
                newnum = (ThreeMMU32)track.event.tempo;
                newden = 1000 * (ThreeMMU32)division;
                g = gcd(newnum, newden);
                newnum /= g;
                newden /= g;
                if (num != newnum || den != newden)
                {
                    num = newnum;
                    den = newden;
                    rawtempo = track.event.timestamp;
                    tstempo = ts;
                }
            }
        }
        else
        {
            n = three_mm_midi_encode_time(ts - last, encoded);
            if (!append(s, &size, encoded, n) || !append(s, &size, track.event.send, (ThreeMMU32)track.event.count))
                return 0;
        }
        /* Legacy advances last time for a tempo-only event too. */
        last = ts;
        if (three_mm_midi_next(&track.parser, &track.event, 1))
            list[minimum] = track;
        else
        {
            --tracks;
            memmove(list + minimum, list + minimum + 1, (tracks - minimum) * sizeof(Track));
            if (!tracks)
                break;
            if (sequential)
            {
                tstempo = last;
                rawtempo = 0;
            }
        }
    }
    *length = size;
    return 1;
}
