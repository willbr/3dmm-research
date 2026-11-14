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
    review_image_highlight_row(result, 2);
    if (result[3] || result[7] != 192 || result[4] != 255 || result[5] || result[6] != 255 || result[8] != 0x7e)
        return 7;
    if (review_image_diff_row(a, a, result, 2) != 0 || result[0] || result[4])
        return 6;
    if (!review_image_hide_match(1, 0, 166464, 100) || review_image_hide_match(1, 1, 166464, 100) ||
        !review_image_hide_match(1, 1664, 166464, 99) || review_image_hide_match(1, 1665, 166464, 99) ||
        review_image_hide_match(0, 0, 166464, 0) || review_image_hide_match(1, 0, 0, 100) ||
        !review_image_hide_match(1, 1, 100, 99) || review_image_hide_match(1, 2, 100, 99))
        return 8;
    puts("Image navigation, match threshold and RGB difference checks passed");
    return 0;
}
