#include "asm_renderer.h"
#include "asm_protocol.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <stdexcept>
namespace
{
std::string Worker()
{
    if (const char *overridePath = SDL_getenv("MARCUS_ASM_WORKER"))
        return overridePath;
    const auto base = SDL_GetBasePath();
    auto sibling = std::string(base ? base : "") + "marcus-asm-worker.exe";
    if (std::filesystem::is_regular_file(sibling))
        return sibling;
#ifdef MARCUS_ASM_WORKER_PATH
    return MARCUS_ASM_WORKER_PATH;
#else
    return sibling;
#endif
}
void Transfer(SDL_IOStream *stream, void *data, std::size_t bytes, bool write)
{
    auto cursor = static_cast<unsigned char *>(data);
    const auto deadline = SDL_GetTicks() + 90000;
    while (bytes)
    {
        const auto count = write ? SDL_WriteIO(stream, cursor, bytes) : SDL_ReadIO(stream, cursor, bytes);
        if (!count)
        {
            if (SDL_GetIOStatus(stream) != SDL_IO_STATUS_NOT_READY || SDL_GetTicks() >= deadline)
                throw std::runtime_error("The x86 ASM renderer exited or timed out.");
            SDL_Delay(1);
            continue;
        }
        cursor += count;
        bytes -= count;
    }
}
} // namespace
struct MarcusASMRenderer::Impl
{
    SDL_Process *process = nullptr;
    SDL_IOStream *input = nullptr, *output = nullptr;
    ~Impl()
    {
        if (!process)
            return;
        SDL_CloseIO(input);
        int status;
        const auto deadline = SDL_GetTicks() + 100;
        while (!SDL_WaitProcess(process, false, &status) && SDL_GetTicks() < deadline)
            SDL_Delay(1);
        if (!SDL_WaitProcess(process, false, &status))
        {
            SDL_KillProcess(process, true);
            SDL_WaitProcess(process, true, &status);
        }
        SDL_DestroyProcess(process);
    }
};
bool MarcusASMRenderer::Available(MM::Error &error)
{
#ifdef _WIN32
    if (std::filesystem::is_regular_file(Worker()))
        return true;
#endif
    error.message =
        "BRender CPU ASM requires the 32-bit marcus-asm-worker.exe. Build build/marcus-x86 or place it beside Marcus.";
    return false;
}
MarcusASMRenderer::MarcusASMRenderer(const std::string &movie, const std::string &resources, const std::string &content)
    : impl(new Impl)
{
    MM::Error error;
    if (!Available(error))
        throw std::runtime_error(error.message);
    const auto executable = Worker();
    const char *args[] = {executable.c_str(), movie.c_str(), resources.c_str(), content.c_str(), nullptr};
    const auto properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char **>(args));
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    impl->process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!impl->process)
        throw std::runtime_error(std::string("Cannot start x86 ASM renderer: ") + SDL_GetError());
    impl->input = SDL_GetProcessInput(impl->process);
    impl->output = SDL_GetProcessOutput(impl->process);
    if (!impl->input || !impl->output)
        throw std::runtime_error("Cannot open x86 ASM renderer streams.");
    std::uint32_t hello[2] = {};
    Transfer(impl->output, hello, sizeof(hello), false);
    if (hello[0] != MarcusASM::Magic || hello[1] != 32)
        throw std::runtime_error("ASM renderer protocol mismatch. Rebuild the 32-bit marcus-asm-worker.exe.");
}
MarcusASMRenderer::~MarcusASMRenderer() = default;
bool MarcusASMRenderer::Render(int scene, int frame, const MM::PreviewView &view, MM::PreviewImage &image,
                               MM::Error &error, const std::vector<MarcusASM::PropOffset> &offsets,
                               std::int32_t firstFrame, std::int32_t lastFrame)
{
    try
    {
        auto request = MarcusASM::Encode(scene, frame, view);
        request.propCount = static_cast<std::uint32_t>(offsets.size());
        request.firstFrame = firstFrame;
        request.lastFrame = lastFrame;
        Transfer(impl->input, &request, sizeof(request), true);
        if (!offsets.empty()) Transfer(impl->input, const_cast<MarcusASM::PropOffset *>(offsets.data()), offsets.size() * sizeof(offsets[0]), true);
        SDL_FlushIO(impl->input);
        MarcusASM::Reply reply;
        Transfer(impl->output, &reply, sizeof(reply), false);
        if (reply.magic != MarcusASM::Magic || reply.errorBytes > 65536 || reply.depth > 1 || reply.width > 4096 ||
            reply.height > 4096)
            throw std::runtime_error("Invalid x86 ASM renderer response.");
        if (reply.errorBytes)
        {
            error.message.resize(reply.errorBytes);
            Transfer(impl->output, error.message.data(), reply.errorBytes, false);
            return false;
        }
        if (reply.width != view.width || reply.height != view.height)
            throw std::runtime_error("ASM renderer returned unexpected dimensions.");
        image = {};
        image.width = reply.width;
        image.height = reply.height;
        image.coveredPixels = reply.covered;
        image.camera = reply.camera;
        image.projection = reply.projection;
        image.sceneCamera = reply.sceneCamera;
        image.rgba.resize(std::size_t(reply.width) * reply.height * 4);
        Transfer(impl->output, image.rgba.data(), image.rgba.size(), false);
        if (reply.depth)
        {
            image.depth.resize(std::size_t(reply.width) * reply.height);
            Transfer(impl->output, image.depth.data(), image.depth.size() * sizeof(float), false);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        error.message = e.what();
        return false;
    }
}
