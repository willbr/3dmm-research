#ifndef MM_PREVIEW_BUFFER_VIEW_H
#define MM_PREVIEW_BUFFER_VIEW_H
#include <cstddef>
namespace MM
{
namespace Internal
{
/* Borrowed typed view over a C-owned buffer. Reacquire after owner mutation. */
template <class T> class PreviewBufferView
{
    const T *values = nullptr;
    std::size_t count = 0;

  public:
    void Reset(const T *p, std::size_t n)
    {
        values = p;
        count = n;
    }
    const T *data() const
    {
        return values;
    }
    std::size_t size() const
    {
        return count;
    }
    bool empty() const
    {
        return !count;
    }
    const T &operator[](std::size_t i) const
    {
        return values[i];
    }
    const T *begin() const
    {
        return values;
    }
    const T *end() const
    {
        return count ? values + count : values;
    }
};
} // namespace Internal
} // namespace MM
#endif
