#include "retail_scroll.h"

void RetailScrollPlayback::Reset()
{
    three_mm_scroll_reset(&_backend);
}
RetailScrollUpdate RetailScrollPlayback::Update(double milliseconds, bool needsScroll)
{
    const auto update = three_mm_scroll_update(&_backend, milliseconds, needsScroll);
    return {update.step != 0, update.holding != 0};
}
double RetailScrollPlayback::NominalSeconds(int pixels)
{
    return three_mm_scroll_nominal_seconds(pixels);
}
