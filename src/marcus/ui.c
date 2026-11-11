#include "ui.h"
#include "ui_vectors.h"
#include <stdlib.h>
#include <string.h>

static void colour(MarcusUI *ui, unsigned long value)
{
    SDL_SetRenderDrawColor(ui->renderer, (unsigned char)(value >> 16), (unsigned char)(value >> 8),
                           (unsigned char)value, (unsigned char)(255 * ui->opacity));
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
    ui->opacity = 1;
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
void ui_opacity(MarcusUI *ui, float opacity)
{
    ui->opacity = opacity < 0 ? 0 : opacity > 1 ? 1 : opacity;
    SDL_SetRenderDrawBlendMode(ui->renderer, ui->opacity < 1 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    if (ui->font)
        SDL_SetTextureAlphaMod(ui->font, (unsigned char)(255 * ui->opacity));
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
static void button_label(MarcusUI *ui, float x, float y, float w, float h, const char *text, unsigned long value)
{
    const unsigned char *p;
    unsigned int code;
    MarcusGlyph *glyph;
    float advance, left, top, right, bottom, glyph_left, glyph_top, available;
    int visible, length;
    available = w - 18;
    if (available <= 0)
        return;
    if (!ui->font)
    {
        length = (int)(available / 8);
        if (length > 255)
            length = 255;
        if ((size_t)length > SDL_strlen(text))
            length = (int)SDL_strlen(text);
        ui_label(ui, SDL_floorf(x + (w - length * 8) / 2), SDL_floorf(y + (h - 8) / 2) - 4, (float)length * 8, text,
                 value);
        return;
    }
    /* Centre the visible glyph bounds, including single symbols such as + and -. */
    advance = left = top = right = bottom = 0;
    visible = 0;
    p = (const unsigned char *)text;
    while (*p)
    {
        code = next_character(&p);
        if (code < 32 || code > 255)
            code = '?';
        glyph = &ui->glyphs[code - 32];
        if (advance + glyph->advance > available)
            break;
        if (glyph->source.w > 0 && glyph->source.h > 0)
        {
            glyph_left = advance + glyph->bearing_x;
            glyph_top = 14.0f - glyph->bearing_y;
            if (!visible || glyph_left < left)
                left = glyph_left;
            if (!visible || glyph_top < top)
                top = glyph_top;
            if (!visible || glyph_left + glyph->source.w > right)
                right = glyph_left + glyph->source.w;
            if (!visible || glyph_top + glyph->source.h > bottom)
                bottom = glyph_top + glyph->source.h;
            visible = 1;
        }
        advance += glyph->advance;
    }
    if (visible)
        ui_label(ui, SDL_floorf(x + (w - (right - left)) / 2 - left), SDL_floorf(y + (h - (bottom - top)) / 2 - top),
                 advance, text, value);
}
void ui_button_sized(MarcusUI *ui, float x, float y, float w, float h, const char *text, int active, int enabled)
{
    unsigned long background;
    background = active ? 0x375e64 : 0x33363b;
    if (enabled && ui_inside(ui->mouse_x, ui->mouse_y, x, y, w, h))
        background = active ? 0x467c82 : 0x45494f;
    ui_fill(ui, x, y, w, h, background);
    ui_line(ui, x, y + h - 1, x + w, y + h - 1, active ? 0x71c6c0 : 0x45494f);
    button_label(ui, x, y, w, h, text, enabled ? 0xe3e5e8 : 0x737780);
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
static void draw_vector_scaled(MarcusUI *ui, float x, float y, const MarcusVectorVertex *source, int count,
                               float scale_x, float scale_y, float brightness, int enabled)
{
    SDL_Vertex vertices[MARCUS_VECTOR_MAX_VERTICES];
    float red, green, blue, grey;
    int i;
    for (i = 0; i < count; ++i)
    {
        red = ((source[i].colour >> 16) & 255) / 255.0f;
        green = ((source[i].colour >> 8) & 255) / 255.0f;
        blue = (source[i].colour & 255) / 255.0f;
        if (!enabled)
        {
            grey = (red + green + blue) / 3;
            red = green = blue = grey;
        }
        red *= brightness;
        green *= brightness;
        blue *= brightness;
        vertices[i].position.x = x + source[i].x * scale_x;
        vertices[i].position.y = y + source[i].y * scale_y;
        vertices[i].color.r = red > 1 ? 1 : red;
        vertices[i].color.g = green > 1 ? 1 : green;
        vertices[i].color.b = blue > 1 ? 1 : blue;
        vertices[i].color.a = ui->opacity;
        vertices[i].tex_coord.x = vertices[i].tex_coord.y = 0;
    }
    SDL_RenderGeometry(ui->renderer, NULL, vertices, count, NULL, 0);
}
static void draw_vector(MarcusUI *ui, float x, float y, const MarcusVectorVertex *source, int count, float brightness,
                        int enabled)
{
    draw_vector_scaled(ui, x, y, source, count, UI_PLAY_WIDTH / MARCUS_VECTOR_WIDTH,
                       UI_PLAY_HEIGHT / MARCUS_VECTOR_HEIGHT, brightness, enabled);
}
void ui_frame_button(MarcusUI *ui, float x, float y, int action, int enabled, const char *hint)
{
    static const MarcusVectorVertex *const artwork[4] = {
        marcus_vector_first_frame, marcus_vector_previous_frame, marcus_vector_next_frame, marcus_vector_last_frame
    };
    static const int counts[4] = {
        MARCUS_VECTOR_FIRST_FRAME_COUNT, MARCUS_VECTOR_PREVIOUS_FRAME_COUNT,
        MARCUS_VECTOR_NEXT_FRAME_COUNT, MARCUS_VECTOR_LAST_FRAME_COUNT
    };
    int hovered, pressed;
    float brightness;
    if (action < 0 || action > 3)
        return;
    hovered = ui_inside(ui->mouse_x, ui->mouse_y, x, y, 28, UI_NAVIGATION_HEIGHT);
    pressed = enabled && hovered && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK);
    brightness = !enabled ? 0.55f : pressed ? 0.80f : hovered ? 1.10f : 1;
    draw_vector_scaled(ui, x, y, artwork[action], counts[action], 1, UI_NAVIGATION_HEIGHT / 20.0f,
                       brightness, enabled);
    if (hovered)
        ui->hint = hint;
}
void ui_settings_button(MarcusUI *ui, float x, float y, float size, int active)
{
    float angle, next, radius, next_radius, center_x, center_y, scale;
    int pass, index, hovered;
    unsigned long tint;
    scale = size / UI_SETTINGS_SIZE;
    hovered = ui_inside(ui->mouse_x, ui->mouse_y, x, y, size, size);
    for (pass = 0; pass < 2; ++pass)
    {
        center_x = x + size / 2 + (pass ? 0 : 1);
        center_y = y + size / 2 + (pass ? 0 : 1);
        tint = pass ? (hovered || active ? 0x78cbc4 : 0xdde0e5) : 0;
        for (index = 0; index < 32; ++index)
        {
            angle = index * 0.19634954f;
            next = (index + 1) * 0.19634954f;
            radius = (index % 4 == 1 || index % 4 == 2 ? 14 : 11) * scale;
            next_radius = ((index + 1) % 4 == 1 || (index + 1) % 4 == 2 ? 14 : 11) * scale;
            ui_line(ui, center_x + SDL_cosf(angle) * radius, center_y + SDL_sinf(angle) * radius,
                    center_x + SDL_cosf(next) * next_radius, center_y + SDL_sinf(next) * next_radius, tint);
        }
        for (index = 0; index < 24; ++index)
        {
            angle = index * 0.26179939f;
            next = (index + 1) * 0.26179939f;
            ui_line(ui, center_x + SDL_cosf(angle) * 4.5f * scale, center_y + SDL_sinf(angle) * 4.5f * scale,
                    center_x + SDL_cosf(next) * 4.5f * scale, center_y + SDL_sinf(next) * 4.5f * scale, tint);
        }
    }
    if (hovered)
        ui->hint = "Rendering settings";
}
void ui_play_button(MarcusUI *ui, float x, float y, int playing, int waiting, int enabled)
{
    int hovered, pressed;
    float brightness, offset;
    hovered = enabled && ui_inside(ui->mouse_x, ui->mouse_y, x, y, UI_PLAY_WIDTH, UI_PLAY_HEIGHT);
    pressed = hovered && (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_LMASK);
    brightness = !enabled ? 0.65f : pressed ? 0.82f : hovered ? 1.12f : 1;
    offset = pressed ? 1 : 0;
    draw_vector(ui, x, y, marcus_vector_frame, MARCUS_VECTOR_FRAME_COUNT, brightness, enabled);
    if (playing && waiting)
        draw_vector(ui, x, y + offset, marcus_vector_continue, MARCUS_VECTOR_CONTINUE_COUNT, brightness, enabled);
    else if (playing)
        draw_vector(ui, x, y + offset, marcus_vector_pause, MARCUS_VECTOR_PAUSE_COUNT, brightness, enabled);
    else
        draw_vector(ui, x, y + offset, marcus_vector_play, MARCUS_VECTOR_PLAY_COUNT, brightness, enabled);
    if (hovered)
        ui->hint = playing && waiting ? "Continue the authored pause" : playing ? "Pause" : "Play";
}
