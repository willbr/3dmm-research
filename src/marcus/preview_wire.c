#include "preview_wire.h"
#include <string.h>

#define MAX_DIMENSION 1024UL

static MMU32 read_word(const unsigned char *p)
{
    return (MMU32)p[0] | ((MMU32)p[1] << 8) | ((MMU32)p[2] << 16) | ((MMU32)p[3] << 24);
}

static void write_word(unsigned char *p, MMU32 word)
{
    unsigned i;
    for (i = 0; i < MARCUS_PREVIEW_PREFIX_BYTES; ++i)
        p[i] = (unsigned char)(word >> (8 * i));
}

static int valid_sizes(const MarcusPreviewPacket *packet)
{
    return packet->width <= MAX_DIMENSION && packet->height <= MAX_DIMENSION &&
        packet->title.size <= MARCUS_PREVIEW_MAX_TEXT && packet->details.size <= MARCUS_PREVIEW_MAX_TEXT &&
        packet->error.size <= MARCUS_PREVIEW_MAX_TEXT;
}

static size_t packet_size(const MarcusPreviewPacket *packet)
{
    return MARCUS_PREVIEW_HEADER_BYTES + packet->title.size + packet->details.size +
        packet->error.size + packet->rgba.size;
}

static const unsigned char *read_bytes(MarcusPreviewBytes *span, const unsigned char *start)
{
    span->data = start;
    return start + span->size;
}

int marcus_preview_wire_read(const unsigned char *bytes, size_t size, MarcusPreviewPacket *out)
{
    MarcusPreviewPacket packet;
    if (size < MARCUS_PREVIEW_HEADER_BYTES || memcmp(bytes, "MPR1", 4) != 0)
        return MARCUS_PREVIEW_WIRE_HEADER;
    packet.width = read_word(bytes + 4);
    packet.height = read_word(bytes + 8);
    packet.title.size = read_word(bytes + 12);
    packet.details.size = read_word(bytes + 16);
    packet.error.size = read_word(bytes + 20);
    if (!valid_sizes(&packet))
        return MARCUS_PREVIEW_WIRE_LIMIT;
    packet.pitch = (size_t)packet.width * 4;
    packet.rgba.size = packet.pitch * packet.height;
    if (packet_size(&packet) != size)
        return MARCUS_PREVIEW_WIRE_LIMIT;
    bytes = read_bytes(&packet.title, bytes + MARCUS_PREVIEW_HEADER_BYTES);
    bytes = read_bytes(&packet.details, bytes);
    bytes = read_bytes(&packet.error, bytes);
    packet.rgba.data = bytes;
    *out = packet;
    return MARCUS_PREVIEW_WIRE_OK;
}

static int write_bytes(const MarcusPreviewBytes *span, void *context, MarcusPreviewWrite write)
{
    return !span->size || write(context, span->data, span->size);
}

int marcus_preview_wire_write(const MarcusPreviewPacket *packet, int framed, void *context, MarcusPreviewWrite write)
{
    unsigned char prefix[MARCUS_PREVIEW_PREFIX_BYTES], header[MARCUS_PREVIEW_HEADER_BYTES];
    const unsigned char *pixels = packet->rgba.data;
    size_t row_bytes;
    MMU32 row;
    if (!valid_sizes(packet))
        return 0;
    row_bytes = (size_t)packet->width * 4;
    if (packet->rgba.size != row_bytes * packet->height ||
        (packet->rgba.size && (!pixels || packet->pitch < row_bytes)))
        return 0;
    memcpy(header, "MPR1", 4);
    write_word(header + 4, packet->width);
    write_word(header + 8, packet->height);
    write_word(header + 12, (MMU32)packet->title.size);
    write_word(header + 16, (MMU32)packet->details.size);
    write_word(header + 20, (MMU32)packet->error.size);
    if (framed)
    {
        write_word(prefix, (MMU32)packet_size(packet));
        if (!write(context, prefix, sizeof(prefix)))
            return 0;
    }
    if (!write(context, header, sizeof(header)) || !write_bytes(&packet->title, context, write) ||
        !write_bytes(&packet->details, context, write) || !write_bytes(&packet->error, context, write))
        return 0;
    if (packet->pitch == row_bytes || !packet->rgba.size)
        return write_bytes(&packet->rgba, context, write);
    for (row = 0; row < packet->height; ++row)
        if (!write(context, pixels + row * packet->pitch, row_bytes))
            return 0;
    return 1;
}

size_t marcus_preview_wire_frame_size(const unsigned char prefix[MARCUS_PREVIEW_PREFIX_BYTES], size_t limit)
{
    const MMU32 bytes = read_word(prefix);
    return bytes >= MARCUS_PREVIEW_HEADER_BYTES && bytes <= limit ? bytes : 0;
}

static int file_write(void *context, const void *bytes, size_t size)
{
    return fwrite(bytes, 1, size, context) == size;
}

int marcus_preview_wire_write_file(const MarcusPreviewPacket *packet, int framed, FILE *file)
{
    return marcus_preview_wire_write(packet, framed, file, file_write) && fflush(file) == 0;
}

void marcus_preview_packet_error(MarcusPreviewPacket *packet, const char *error)
{
    memset(packet, 0, sizeof(*packet));
    packet->error.data = error;
    packet->error.size = strlen(error);
}
