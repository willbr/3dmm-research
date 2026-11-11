#ifndef RETAIL_SCROLL_H
#define RETAIL_SCROLL_H
#include "three_mm/playback.h"

/* Transitional C++ facade over the shared C89 playback scrolling branch.
   The host supplies layout/overflow facts and applies one pixel when step=true.
   Wall-clock lateness is discarded, as with retail's default slipping clock. */
struct RetailScrollUpdate
{
    bool step = false;
    bool holding = false;
};

class RetailScrollPlayback
{
  public:
    RetailScrollPlayback()
    {
        three_mm_scroll_reset(&_backend);
    }
    void Reset();
    RetailScrollUpdate Update(double milliseconds, bool needsScroll);
    static double NominalSeconds(int pixels);

  private:
    ThreeMMScrollPlayback _backend;
};
#endif
