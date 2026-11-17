/* Native user-service value translation for legacy callers. */
#include "platform_users.h"
#include "mm/native_users.h"

KauaiUsers::UserName KauaiUsers::QueryName()
{
    MMNativeUserName native;
    mm_native_user_name(&native);
    try {
        UserName result;
        result.status = static_cast<NameStatus>(native.status);
        result.name = native.name;
        result.provider = native.provider;
        result.message = native.message;
        result.error = native.error;
        return result;
    } catch (...) { return {}; }
}
std::size_t KauaiUsers::DataRoot(NameChar *output, std::size_t capacity)
{
    return mm_native_user_data_root(output, capacity);
}
std::string KauaiUsers::PosixDataRoot(PosixHost host, const char *home, const char *xdg)
{
    const auto length = mm_native_posix_user_data_root(static_cast<int>(host), home, xdg, nullptr, 0);
    if (!length) return {};
    std::string result(length + 1, '\0');
    mm_native_posix_user_data_root(static_cast<int>(host), home, xdg, &result[0], result.size());
    result.resize(length);
    return result;
}
