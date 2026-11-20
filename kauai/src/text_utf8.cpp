/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "text_utf8.h"
#include "mm/text.h"
#include <exception>

namespace KauaiText
{
namespace
{
template <class Container> struct Utf8Output
{
    Container &value;
    std::exception_ptr exception;

    static int Append(void *p, const typename Container::value_type *units, std::size_t count)
    {
        auto &output = *static_cast<Utf8Output *>(p);
        try
        {
            for (std::size_t i = 0; i < count; ++i)
                output.value.push_back(units[i]);
            return 1;
        }
        catch (...)
        {
            output.exception = std::current_exception();
            return -1;
        }
    }

    bool Check(int status)
    {
        if (exception)
            std::rethrow_exception(exception);
        return status > 0;
    }
};
}

bool EncodeUtf8(const std::vector<std::uint16_t> &units, std::string &result)
{
    result.clear();
    Utf8Output<std::string> output{result, {}};
    return output.Check(mm_text_encode_utf8(units.data(), units.size(), &output, output.Append));
}

bool DecodeUtf8(const std::string &text, std::vector<std::uint16_t> &units)
{
    units.clear();
    Utf8Output<std::vector<std::uint16_t>> output{units, {}};
    return output.Check(mm_text_decode_utf8(text.data(), text.size(), &output, output.Append));
}
} // namespace KauaiText
