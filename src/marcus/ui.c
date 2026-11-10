#include "ui.h"
#include <stdlib.h>
#include <string.h>

static void colour(MarcusUI *ui, unsigned long value)
{
    SDL_SetRenderDrawColor(ui->renderer, (unsigned char)(value >> 16), (unsigned char)(value >> 8),
                           (unsigned char)value, 255);
}
int ui_init(MarcusUI *ui, SDL_Renderer *renderer, const char *font_path)
{
    FT_Library library;
    FT_Face face;
    unsigned char *pixels;
    int code, x, y, row, column, gx, gy;
    const char *candidates[6];
    SDL_Surface *surface;
    MarcusGlyph *glyph;
    memset(ui, 0, sizeof(*ui));
    ui->renderer = renderer;
    candidates[0] = font_path ? font_path : "";
    candidates[1] = "C:/Windows/Fonts/segoeui.ttf";
    candidates[2] = "/System/Library/Fonts/Supplemental/Arial.ttf";
    candidates[3] = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    candidates[4] = "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf";
    candidates[5] = NULL;
    if (FT_Init_FreeType(&library))
        return 0;
    face = NULL;
    for (code = 0; candidates[code]; ++code)
        if (candidates[code][0] && !FT_New_Face(library, candidates[code], 0, &face))
            break;
    if (!face)
    {
        FT_Done_FreeType(library);
        return 1; /* SDL's bitmap text remains available on minimal hosts. */
    }
    FT_Set_Pixel_Sizes(face, 0, 14);
    pixels = (unsigned char *)calloc(512 * 256, 4);
    if (!pixels)
    {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 0;
    }
    for (code = 32; code < 256; ++code)
    {
        if (FT_Load_Char(face, (unsigned long)code, FT_LOAD_RENDER))
            continue;
        column = (code - 32) % 32;
        row = (code - 32) / 32;
        gx = column * 16;
        gy = row * 32;
        glyph = &ui->glyphs[code - 32];
        glyph->source.x = (float)gx;
        glyph->source.y = (float)gy;
        glyph->source.w = (float)face->glyph->bitmap.width;
        glyph->source.h = (float)face->glyph->bitmap.rows;
        glyph->bearing_x = face->glyph->bitmap_left;
        glyph->bearing_y = face->glyph->bitmap_top;
        glyph->advance = (int)(face->glyph->advance.x >> 6);
        for (y = 0; y < (int)face->glyph->bitmap.rows && y < 32; ++y)
            for (x = 0; x < (int)face->glyph->bitmap.width && x < 16; ++x)
            {
                pixels[((gy + y) * 512 + gx + x) * 4] = 255;
                pixels[((gy + y) * 512 + gx + x) * 4 + 1] = 255;
                pixels[((gy + y) * 512 + gx + x) * 4 + 2] = 255;
                pixels[((gy + y) * 512 + gx + x) * 4 + 3] =
                    face->glyph->bitmap.buffer[y * face->glyph->bitmap.pitch + x];
            }
    }
    surface = SDL_CreateSurfaceFrom(512, 256, SDL_PIXELFORMAT_RGBA32, pixels, 512 * 4);
    if (surface)
    {
        ui->font = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
    }
    free(pixels);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    if (ui->font)
    {
        SDL_SetTextureBlendMode(ui->font, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(ui->font, SDL_SCALEMODE_LINEAR);
    }
    return 1;
}
void ui_destroy(MarcusUI *ui)
{
    SDL_DestroyTexture(ui->font);
    ui->font = NULL;
}
void ui_fill(MarcusUI *ui, float x, float y, float w, float h, unsigned long value)
{
    SDL_FRect rect;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;
    colour(ui, value);
    SDL_RenderFillRect(ui->renderer, &rect);
}
void ui_line(MarcusUI *ui, float x, float y, float x2, float y2, unsigned long value)
{
    colour(ui, value);
    SDL_RenderLine(ui->renderer, x, y, x2, y2);
}
static unsigned int next_character(const unsigned char **text)
{
    unsigned int result;
    const unsigned char *p;
    p = *text;
    result = *p++;
    if (result >= 0xc2 && result <= 0xc3 && (*p & 0xc0) == 0x80)
        result = ((result & 31) << 6) | (*p++ & 63);
    else if (result >= 128)
    {
        while ((*p & 0xc0) == 0x80)
            ++p;
        result = '?';
    }
    *text = p;
    return result;
}
void ui_label(MarcusUI *ui, float x, float y, float width, const char *text, unsigned long value)
{
    const unsigned char *p;
    unsigned int code;
    float end;
    MarcusGlyph *glyph;
    SDL_FRect destination;
    char fallback[256];
    int length;
    end = x + width;
    if (!ui->font)
    {
        length = (int)(width / 8);
        if (length < 0)
            return;
        if (length > 255)
            length = 255;
        SDL_strlcpy(fallback, text, (size_t)length + 1);
        colour(ui, value);
        SDL_RenderDebugText(ui->renderer, x, y + 4, fallback);
        return;
    }
    SDL_SetTextureColorMod(ui->font, (unsigned char)(value >> 16), (unsigned char)(value >> 8), (unsigned char)value);
    p = (const unsigned char *)text;
    while (*p)
    {
        code = next_character(&p);
        if (code < 32 || code > 255)
            code = '?';
        glyph = &ui->glyphs[code - 32];
        if (x + glyph->advance > end)
            break;
        destination.x = x + glyph->bearing_x;
        destination.y = y + 14 - glyph->bearing_y;
        destination.w = glyph->source.w;
        destination.h = glyph->source.h;
        SDL_RenderTexture(ui->renderer, ui->font, &glyph->source, &destination);
        x += glyph->advance;
    }
}
void ui_text(MarcusUI *ui, float x, float y, const char *text, unsigned long value)
{
    ui_label(ui, x, y, 4096, text, value);
}
int ui_inside(float px, float py, float x, float y, float w, float h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}
void ui_button(MarcusUI *ui, float x, float y, float w, const char *text, int active, int enabled)
{
    ui_button_sized(ui, x, y, w, 28, text, active, enabled);
}
void ui_button_sized(MarcusUI *ui, float x, float y, float w, float h, const char *text, int active, int enabled)
{
    unsigned long background;
    background = active ? 0x375e64 : 0x33363b;
    if (enabled && ui_inside(ui->mouse_x, ui->mouse_y, x, y, w, h))
        background = active ? 0x467c82 : 0x45494f;
    ui_fill(ui, x, y, w, h, background);
    ui_line(ui, x, y + h - 1, x + w, y + h - 1, active ? 0x71c6c0 : 0x45494f);
    ui_label(ui, x + 9, y + (h - 18) / 2, w - 18, text, enabled ? 0xe3e5e8 : 0x737780);
}
void ui_navigation_button(MarcusUI *ui, float x, float y, int action, int enabled, const char *hint)
{
    int row;
    float center;
    unsigned long colour;
    ui_button_sized(ui, x, y, 28, UI_NAVIGATION_HEIGHT, "", 0, enabled);
    colour = enabled ? 0xe3e5e8 : 0x737780;
    center = x + 14;
    if (action == 0)
        center += 2;
    if (action == 3)
        center -= 2;
    for (row = -5; row <= 5; ++row)
    {
        if (action <= 1)
            ui_line(ui, center - 4 + abs(row) * 8 / 5, y + 10 + row, center + 4, y + 10 + row, colour);
        else
            ui_line(ui, center - 4, y + 10 + row, center + 4 - abs(row) * 8 / 5, y + 10 + row, colour);
    }
    if (action == 0)
        ui_fill(ui, x + 6, y + 4, 2, 13, colour);
    if (action == 3)
        ui_fill(ui, x + 20, y + 4, 2, 13, colour);
    if (ui_inside(ui->mouse_x, ui->mouse_y, x, y, 28, UI_NAVIGATION_HEIGHT))
        ui->hint = hint;
}
