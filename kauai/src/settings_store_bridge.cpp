/* Existing C++ settings values/callbacks translated at the C89 boundary. */
#include "settings_store.h"
#include "settings_store_posix.h"
#include "mm/native_settings.h"
#include <exception>
#include <new>
#include <utility>
#ifdef _WIN32
#include "settings_store_win.h"
#endif
namespace {
using namespace KauaiSettings;
static_assert(static_cast<int>(Type::Unknown) == MM_SETTINGS_UNKNOWN &&
    static_cast<int>(ReadResult::Error) == MM_SETTINGS_ERROR &&
    static_cast<int>(PosixHost::MacOS) == MM_SETTINGS_MACOS, "settings enum profiles");
struct PolicyContext {
    const Backend &backend;
    const Request &request;
    MMSettingsRequest &value;
    bool *noValue, *wrongType;
    std::exception_ptr error;
};
void RefreshRequest(PolicyContext &context)
{
    context.value.type = static_cast<int>(context.request.type);
    context.value.write = context.request.write;
    context.value.set_default = context.request.setDefault;
    context.value.machine = context.request.machine;
}
void *PolicyOpen(void *opaque, const void *key, int machine, int *created)
{
    auto &context = *static_cast<PolicyContext *>(opaque);
    try {
        bool value = false;
        auto handle = context.backend.open(static_cast<const NameChar *>(key), machine != 0, &value);
        RefreshRequest(context);
        *created = value;
        return handle;
    } catch (...) { context.error = std::current_exception(); return nullptr; }
}
int PolicyRead(void *opaque, void *handle, const void *name, void *data, MMU32 *bytes, int *kind)
{
    auto &context = *static_cast<PolicyContext *>(opaque);
    try {
        auto type = static_cast<Type>(*kind);
        auto result = context.backend.read(handle, static_cast<const NameChar *>(name), data, bytes, &type);
        RefreshRequest(context);
        *kind = static_cast<int>(type);
        return static_cast<int>(result);
    } catch (...) { context.error = std::current_exception(); return MM_SETTINGS_ERROR; }
}
int PolicyWrite(void *opaque, void *handle, const void *name, const void *data, MMU32 bytes, int kind)
{
    auto &context = *static_cast<PolicyContext *>(opaque);
    try {
        const auto result = context.backend.write(handle, static_cast<const NameChar *>(name), data, bytes, static_cast<Type>(kind));
        RefreshRequest(context);
        return result;
    }
    catch (...) { context.error = std::current_exception(); return 0; }
}
void PolicyClose(void *opaque, void *handle)
{
    auto &context = *static_cast<PolicyContext *>(opaque);
    try { context.backend.close(handle); }
    catch (...) { std::terminate(); } // The original RAII destructor was noexcept.
}
void PolicyPublish(void *opaque, int flag, int value)
{
    auto &context = *static_cast<PolicyContext *>(opaque);
    if (context.error) return;
    if (flag == MM_SETTINGS_NO_VALUE) *context.noValue = value != 0;
    else *context.wrongType = value != 0;
    RefreshRequest(context);
}
struct NativeOwner {
    MMNativeSettingsContext *context;
    MMSettingsBackend backend{};
    NativeOwner() {
#if defined(DEBUG) || defined(STUDIO_TEST_TOOLS)
        context = mm_native_settings_create(1);
#else
        context = mm_native_settings_create(0);
#endif
        if (!context) throw std::bad_alloc();
        mm_native_settings_backend(context, &backend);
    }
    ~NativeOwner() { mm_native_settings_destroy(context); }
};
NativeOwner &Native() { static NativeOwner owner; return owner; }
Handle NativeOpen(const NameChar *key, bool machine, bool *created)
{
    if (!created) return nullptr;
    try {
        auto &backend = Native().backend;
        int value = 0;
        auto handle = backend.open(backend.context, key, machine, &value);
        if (handle) *created = value != 0;
        return handle;
    } catch (const std::bad_alloc &) { return nullptr; }
}
ReadResult NativeRead(Handle handle, const NameChar *name, void *data, std::uint32_t *bytes, Type *type)
{
    auto &backend = Native().backend;
    int kind = MM_SETTINGS_UNKNOWN;
    const auto result = backend.read(backend.context, handle, name, data, bytes, type ? &kind : nullptr);
    if (type && result == MM_SETTINGS_FOUND) *type = static_cast<Type>(kind);
    return static_cast<ReadResult>(result);
}
bool NativeWrite(Handle handle, const NameChar *name, const void *data, std::uint32_t bytes, Type type)
{
    auto &backend = Native().backend;
    return backend.write(backend.context, handle, name, data, bytes, static_cast<int>(type)) != 0;
}
void NativeClose(Handle handle)
{
    auto &backend = Native().backend;
    backend.close(backend.context, handle);
}
const Backend nativeBackend{NativeOpen, NativeRead, NativeWrite, NativeClose};
#ifdef _WIN32
struct ValuesContext { std::vector<WindowsValue> &values; std::exception_ptr error; };
int VisitValue(void *opaque, const MMNativePathChar *name, size_t count, int type, const unsigned char *data, size_t bytes)
{
    auto &context = *static_cast<ValuesContext *>(opaque);
    try {
        WindowsValue value;
        value.name.assign(name, count); value.type = static_cast<Type>(type);
        if (bytes) value.bytes.assign(data, data + bytes);
        context.values.push_back(std::move(value));
        return 1;
    } catch (...) { context.error = std::current_exception(); return 0; }
}
#endif
}
bool KauaiSettings::Access(const Backend &backend, const NameChar *key, const NameChar *name,
    void *data, std::uint32_t capacity, const Request &request, bool *noValue, bool *wrongType)
{
    int missing = 0, wrong = 0;
    MMSettingsRequest value{static_cast<int>(request.type), request.write, request.setDefault, request.machine, sizeof(NameChar)};
    PolicyContext context{backend, request, value, noValue, wrongType, {}};
    const MMSettingsBackend callbacks{&context, PolicyOpen, PolicyRead, PolicyWrite, PolicyClose};
    const auto ok = mm_settings_access_observed(backend.open && backend.read && backend.write && backend.close ? &callbacks : nullptr,
        key, name, data, capacity, &value, noValue ? &missing : nullptr, wrongType ? &wrong : nullptr, &context, PolicyPublish);
    if (context.error) std::rethrow_exception(context.error);
    return ok != 0;
}
std::string KauaiSettings::ConfigurationRoot(PosixHost host, bool machine, const char *home, const char *xdg)
{
    auto length = mm_settings_configuration_root(static_cast<int>(host), machine, home, xdg, nullptr, 0);
    if (!length) return {};
    std::string result(length + 1, '\0');
    mm_settings_configuration_root(static_cast<int>(host), machine, home, xdg, &result[0], result.size());
    result.resize(length);
    return result;
}
#ifdef _WIN32
const KauaiSettings::Backend &KauaiSettings::WindowsBackend() { return nativeBackend; }
bool KauaiSettings::ReadWindowsValues(const NameChar *key, bool machine, std::vector<WindowsValue> &values)
{
    values.clear();
    ValuesContext context{values, {}};
    int allocationError = 0;
    const auto ok = mm_native_settings_values(Native().context, key, machine, &context, VisitValue, &allocationError);
    if (context.error) std::rethrow_exception(context.error);
    if (allocationError) throw std::bad_alloc();
    return ok != 0;
}
#else
const KauaiSettings::Backend &KauaiSettings::PosixBackend() { return nativeBackend; }
#endif
