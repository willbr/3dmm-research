// Exceptions remain inside legacy callbacks until C has closed the handle.
#include "settings_store.h"
#include <cassert>
using namespace KauaiSettings;
namespace {
struct Failure { int callback; };
int failing, opens, reads, writes, closes;
bool observe, mutate, expectedMissing, expectedWrong;
bool *missingFlag, *wrongFlag;
ReadResult readResult = ReadResult::Missing;
Request *changedRequest;
Handle Open(const NameChar *, bool, bool *created)
{
    ++opens;
    if (failing == 1) throw Failure{1};
    if (changedRequest) changedRequest->write = true;
    *created = false;
    return &failing;
}
ReadResult Read(Handle handle, const NameChar *, void *data, std::uint32_t *, Type *type)
{
    assert(handle == &failing); ++reads;
    if (failing == 2) { *static_cast<std::uint32_t *>(data) = 42; throw Failure{2}; }
    if (mutate) *missingFlag = *wrongFlag = true;
    if (readResult == ReadResult::Found) *type = Type::String;
    return readResult;
}
bool Write(Handle handle, const NameChar *, const void *, std::uint32_t, Type)
{
    assert(handle == &failing); ++writes;
    if (failing == 3) throw Failure{3};
    if (mutate) *missingFlag = *wrongFlag = true;
    return true;
}
void Close(Handle handle)
{
    assert(handle == &failing); ++closes;
    if (observe) {
        assert(*missingFlag == expectedMissing && *wrongFlag == expectedWrong);
        *missingFlag = !expectedMissing; *wrongFlag = !expectedWrong;
    }
}
}
int main()
{
    const Backend backend{Open, Read, Write, Close};
    const NameChar key[] = {'k', 0}, name[] = {'v', 0};
    Request request; request.setDefault = true;
    for (failing = 1; failing <= 3; ++failing) {
        opens = reads = writes = closes = 0;
        bool missing = true, wrong = true, caught = false;
        std::uint32_t value = 7;
        try { Access(backend, key, name, &value, sizeof(value), request, &missing, &wrong); }
        catch (const Failure &error) { assert(error.callback == failing); caught = true; }
        assert(caught && missing && !wrong && opens == 1);
        assert(closes == (failing == 1 ? 0 : 1));
        assert(reads == (failing == 1 ? 0 : 1) && writes == (failing == 3 ? 1 : 0));
        assert(value == (failing == 2 ? 42U : 7U));
    }
    failing = 0; observe = true;
    bool missing = false, wrong = true;
    missingFlag = &missing; wrongFlag = &wrong;
    std::uint32_t value = 7;
    request.setDefault = false;
    expectedMissing = true; expectedWrong = false;
    assert(Access(backend, key, name, &value, sizeof(value), request, &missing, &wrong));
    assert(!missing && wrong); // Close's changes survive the return.
    readResult = ReadResult::Found;
    expectedMissing = false; expectedWrong = true;
    assert(Access(backend, key, name, &value, sizeof(value), request, &missing, &wrong));
    assert(missing && !wrong);
    readResult = ReadResult::Error; mutate = true;
    assert(!Access(backend, key, name, &value, sizeof(value), request, &missing, &wrong));
    assert(missing && !wrong);
    request.write = true;
    assert(Access(backend, key, name, &value, sizeof(value), request, &missing, &wrong));
    assert(missing && !wrong);
    mutate = false; request.write = false; readResult = ReadResult::Found;
    bool aliased = true;
    missingFlag = wrongFlag = &aliased; expectedMissing = expectedWrong = false;
    assert(Access(backend, key, name, &value, sizeof(value), request, &aliased, &aliased));
    assert(aliased);
    observe = false; changedRequest = &request;
    const auto previousReads = reads;
    assert(Access(backend, key, name, &value, sizeof(value), request));
    assert(request.write && reads == previousReads);
    return 0;
}
