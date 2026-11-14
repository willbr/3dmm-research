#include "image_view.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ReviewImageView view;
    unsigned char a[] = {10, 20, 30, 0, 0, 128, 255, 255};
    unsigned char b[] = {10, 20, 30, 255, 255, 64, 0, 0};
    unsigned char expected[] = {0, 0, 0, 255, 255, 64, 255, 255};
    unsigned char result[9];
    double pixel_x, pixel_y;
    view.x = 900;
    view.y = 1700;
    view.zoom = 0.125;
    pixel_x = view.x + 73 / view.zoom;
    pixel_y = view.y - 41 / view.zoom;
    review_image_zoom(&view, 3, 73, -41);
    if (fabs(view.x + 73 / view.zoom - pixel_x) > 1e-9 ||
        fabs(view.y - 41 / view.zoom - pixel_y) > 1e-9)
        return 1;
    review_image_pan(&view, 39, -18);
    if (fabs(view.x + 73 / view.zoom - (pixel_x - 13)) > 1e-9 ||
        fabs(view.y - 41 / view.zoom - (pixel_y + 6)) > 1e-9)
        return 2;
    review_image_zoom(&view, 100, 0, 0);
    if (view.zoom != 32)
        return 3;
    review_image_zoom(&view, 0, 0, 0);
    if (view.zoom != 0.005)
        return 4;
    memset(result, 0x7e, sizeof(result));
    if (review_image_diff_row(a, b, result, 2) != 1 || memcmp(result, expected, 8) || result[8] != 0x7e)
        return 5;
    if (review_image_diff_row(a, a, result, 2) != 0 || result[0] || result[4])
        return 6;
    puts("Image navigation and RGB difference checks passed");
    return 0;
}
