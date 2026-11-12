/* Movie title policy converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/movie_title.h"
#include <string.h>
int three_mm_movie_title_init(ThreeMMMovieTitle *t, unsigned unit)
{
    if (!t || (unit != 1 && unit != 2))
        return 0;
    memset(t, 0, sizeof(*t));
    t->unit = unit;
    return 1;
}
int three_mm_movie_title_set(ThreeMMMovieTitle *t, const void *text, ThreeMMU32 count)
{
    if (!t || (t->unit != 1 && t->unit != 2) || count > 255 || (!text && count))
        return 0;
    if (count)
        memmove(t->text, text, count * t->unit);
    memset((unsigned char *)t->text + count * t->unit, 0, t->unit);
    t->count = count;
    return 1;
}
const void *three_mm_movie_title_text(const ThreeMMMovieTitle *t)
{
    return t->text;
}
static unsigned character(const ThreeMMMovieTitle *t, ThreeMMU32 i)
{
    unsigned short value;
    if (t->unit == 1)
        return ((const unsigned char *)t->text)[i];
    memcpy(&value, (const unsigned char *)t->text + i * 2, 2);
    return value;
}
void three_mm_movie_title_strip_extension(ThreeMMMovieTitle *t)
{
    ThreeMMU32 i = t->count;
    while (i && character(t, i) != '.')
        --i;
    if (character(t, i) == '.')
    {
        t->count = i;
        memset((unsigned char *)t->text + i * t->unit, 0, t->unit);
    }
}
void three_mm_movie_title_choose(ThreeMMMovieTitle *t, int file, const ThreeMMMovieTitleServices *s)
{
    if (!file)
    {
        if (!t->count)
        {
            s->default_name(s->context, t);
            s->notify(s->context, t);
        }
        return;
    }
    s->leaf(s->context, t);
    three_mm_movie_title_strip_extension(t);
    if (!t->count)
        s->default_name(s->context, t);
    s->notify(s->context, t);
}
