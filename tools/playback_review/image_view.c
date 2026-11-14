#include "image_view.h"

void review_image_zoom(ReviewImageView *view, double zoom, double dx, double dy)
{
    double old;
    old = view->zoom;
    if (zoom < 0.005)
        zoom = 0.005;
    if (zoom > 32)
        zoom = 32;
    view->x += dx / old - dx / zoom;
    view->y += dy / old - dy / zoom;
    view->zoom = zoom;
}

void review_image_pan(ReviewImageView *view, double dx, double dy)
{
    view->x -= dx / view->zoom;
    view->y -= dy / view->zoom;
}

size_t review_image_diff_row(const unsigned char *a, const unsigned char *b, unsigned char *out, size_t pixels)
{
    size_t x, changed;
    int channel, delta, different;
    changed = 0;
    for (x = 0; x < pixels; ++x)
    {
        different = 0;
        for (channel = 0; channel < 3; ++channel)
        {
            delta = (int)a[x * 4 + channel] - b[x * 4 + channel];
            if (delta < 0)
                delta = -delta;
            out[x * 4 + channel] = (unsigned char)delta;
            different |= delta;
        }
        out[x * 4 + 3] = 255;
        if (different)
            ++changed;
    }
    return changed;
}

void review_image_highlight_row(unsigned char *diff, size_t pixels)
{
    size_t x;
    for (x = 0; x < pixels; ++x)
    {
        diff[x * 4 + 3] = (unsigned char)((diff[x * 4] || diff[x * 4 + 1] || diff[x * 4 + 2]) ? 192 : 0);
        diff[x * 4] = diff[x * 4 + 2] = 255;
        diff[x * 4 + 1] = 0;
    }
}

int review_image_hide_match(int compared, size_t changed, size_t pixels, double threshold)
{
    if (!compared || !pixels || changed > pixels || threshold < 0 || threshold > 100)
        return 0;
    return (double)changed * 100 <= (double)pixels * (100 - threshold);
}
