#ifndef RETAIL_SCROLL_H
#define RETAIL_SCROLL_H

/* Portable adapter to the SDL engine's MoviePlaybackEvaluator scrolling branch.
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
    void Reset();
    RetailScrollUpdate Update(double milliseconds, bool needsScroll);
    static double NominalSeconds(int pixels);

  private:
    bool started = false, holding = false;
    double remaining = 0;
};
#endif
