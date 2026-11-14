#ifndef REVIEW_IMAGE_VIEW_H
#define REVIEW_IMAGE_VIEW_H
#include <stddef.h>

typedef struct ReviewImageView
{
    double x, y, zoom;
} ReviewImageView;

/* Coordinates are image pixels; dx/dy are relative to the pane's centre. */
void review_image_zoom(ReviewImageView *view, double zoom, double dx, double dy);
void review_image_pan(ReviewImageView *view, double dx, double dy);
/* Opaque absolute RGB difference. Alpha does not affect the comparison. */
size_t review_image_diff_row(const unsigned char *a, const unsigned char *b, unsigned char *out, size_t pixels);
#endif
