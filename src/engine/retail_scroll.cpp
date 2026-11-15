#include "retail_scroll.h"

void RetailScrollPlayback::Reset()
{
    mm_scroll_reset(&_backend);
}
RetailScrollUpdate RetailScrollPlayback::Update(double milliseconds, bool needsScroll)
{
    const auto update = mm_scroll_update(&_backend, milliseconds, needsScroll);
    return {update.step != 0, update.holding != 0};
}
double RetailScrollPlayback::NominalSeconds(int pixels)
{
    return mm_scroll_nominal_seconds(pixels);
}
