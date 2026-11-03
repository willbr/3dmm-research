/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "archive.h"
#include <exception>
#include <utility>

namespace Inspector
{
namespace
{
ThreeMM::Options PreviewOptions()
{
    ThreeMM::Options options;
    options.maxDecodedBytes = 64u * 1024 * 1024;
    return options;
}
} // namespace
struct Archive::Impl
{
    ThreeMM::Archive archive{PreviewOptions()};
    std::vector<Chunk> chunks;
};
Archive::Archive() : _impl(new Impl)
{
}
Archive::~Archive() = default;
void Archive::Close()
{
    _impl.reset(new Impl);
}
const std::vector<Chunk> &Archive::Chunks() const
{
    return _impl->chunks;
}
const std::string &Archive::Path() const
{
    return _impl->archive.Path();
}
bool Archive::Open(const std::string &path, std::string &error)
{
    error.clear();
    try
    {
        std::unique_ptr<Impl> opened(new Impl);
        ThreeMM::Error failure;
        if (!opened->archive.Open(path, failure))
        {
            error = failure.message;
            return false;
        }
        opened->chunks = opened->archive.Chunks();
        for (auto &chunk : opened->chunks)
            for (char &ch : chunk.name)
                if (static_cast<unsigned char>(ch) < 32)
                    ch = ' ';
        _impl.swap(opened);
        return true;
    }
    catch (const std::exception &)
    {
        error = "Insufficient memory to open archive.";
        return false;
    }
}
bool Archive::ReadDetail(std::size_t index, std::uint32_t offset, Detail &detail, std::string &error,
                         std::uint32_t firstString)
{
    detail = {};
    error.clear();
    ThreeMM::Error failure;
    ThreeMM::PayloadPage payload;
    if (!_impl->archive.ReadPayload(index, offset, 4096, payload, failure))
    {
        error = failure.message;
        return false;
    }
    detail.totalBytes = payload.totalBytes;
    detail.offset = payload.offset;
    detail.bytes = std::move(payload.bytes);
    if (TypeLabel(_impl->chunks[index].id.type) == "GST ")
    {
        detail.strings.present = true;
        ThreeMM::StringTablePage page;
        if (!_impl->archive.ReadStrings(index, firstString, 128, page, failure))
            detail.strings.error = failure.message + " Raw bytes remain available below.";
        else
        {
            detail.strings.totalEntries = page.totalEntries;
            detail.strings.firstEntry = page.firstEntry;
            detail.strings.extraBytes = page.extraBytes;
            detail.strings.entries = std::move(page.entries);
        }
        return true;
    }
    ThreeMM::RecordHeader record;
    if (!_impl->archive.ReadRecord(index, record, failure))
    {
        detail.fields.push_back(failure.message);
        return true;
    }
    switch (record.kind)
    {
    case ThreeMM::RecordKind::movie:
        detail.fields.push_back("Movie version: " + std::to_string(record.movie.version));
        detail.fields.push_back("Backwards version: " + std::to_string(record.movie.backVersion));
        break;
    case ThreeMM::RecordKind::scene:
        detail.fields.push_back("Scene frames: " + std::to_string(record.scene.firstFrame) + " to " +
                                std::to_string(record.scene.lastFrame));
        detail.fields.push_back("Transition: " + std::to_string(record.scene.transition));
        break;
    case ThreeMM::RecordKind::actor:
        detail.fields.push_back("Actor ID: " + std::to_string(record.actor.actorId));
        detail.fields.push_back("Actor frames: " + std::to_string(record.actor.firstFrame) + " to " +
                                std::to_string(record.actor.lastFrame));
        break;
    case ThreeMM::RecordKind::caption:
        detail.fields.push_back("Caption frames: " + std::to_string(record.caption.firstFrame) + " to " +
                                std::to_string(record.caption.maxFrame));
        detail.fields.push_back("Rich text: " + Identity(record.caption.text));
        break;
    case ThreeMM::RecordKind::opaque:
        detail.fields.push_back("Opaque payload: no structured decoder for this chunk type.");
        break;
    }
    return true;
}
} // namespace Inspector
