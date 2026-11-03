/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "archive.h"
#include "three_mm/preview.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
const ImVec4 accent(0.30f, 0.83f, 0.71f, 1.0f);
std::string DisplayString(const std::string &text)
{
    std::string result;
    for (unsigned char ch : text)
    {
        switch (ch)
        {
        case '\0':
            result += "\\0";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\\':
            result += "\\\\";
            break;
        default:
            if (ch < 32 || ch == 127)
            {
                char escaped[5];
                std::snprintf(escaped, sizeof(escaped), "\\x%02x", ch);
                result += escaped;
            }
            else
                result += static_cast<char>(ch);
        }
    }
    return result;
}
struct DialogResult
{
    std::mutex mutex;
    bool pending = false, ready = false;
    std::string path, error;
};
void SDLCALL FileChosen(void *userdata, const char *const *files, int)
{
    // SDL may invoke this on another thread; never touch ImGui or core here.
    std::unique_ptr<std::shared_ptr<DialogResult>> holder(static_cast<std::shared_ptr<DialogResult> *>(userdata));
    auto state = *holder;
    std::lock_guard<std::mutex> lock(state->mutex);
    state->pending = false;
    state->ready = true;
    if (!files)
        state->error = "The file dialog could not be opened. Paste a path or drop a file instead.";
    else if (files[0])
        state->path = files[0];
}
void Style(float scale)
{
    ImGui::StyleColorsDark();
    auto &s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(20, 16);
    s.FramePadding = ImVec2(9, 7);
    s.ItemSpacing = ImVec2(10, 9);
    s.WindowRounding = 0;
    s.FrameRounding = 4;
    s.ChildRounding = 6;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(.07f, .085f, .10f, 1);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(.09f, .105f, .12f, 1);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(.13f, .15f, .17f, 1);
    s.Colors[ImGuiCol_Button] = ImVec4(.15f, .27f, .27f, 1);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(.18f, .38f, .35f, 1);
    s.Colors[ImGuiCol_Header] = ImVec4(.15f, .31f, .30f, 1);
    s.Colors[ImGuiCol_HeaderHovered] = ImVec4(.18f, .36f, .34f, 1);
    s.Colors[ImGuiCol_TableHeaderBg] = ImVec4(.12f, .14f, .16f, 1);
    s.Colors[ImGuiCol_Text] = ImVec4(.88f, .91f, .93f, 1);
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}
class App
{
    Inspector::Archive archive;
    Inspector::Detail detail;
    std::string error, detailError;
    char path[4096] = {}, filter[128] = {};
    int selected = -1;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *previewTexture = nullptr;
    ThreeMM::Preview preview;
    ThreeMM::PreviewView previewView;
    std::string previewError, previewArchive, previewResources;
    char resourcesPath[4096] = {}, contentPath[4096] = {};
    std::string previewContent;
    int action = 0, cel = 0, sceneFrame = 0;
    bool previewSupported = false, previewReady = false, previewDirty = false;
    bool selectPreviewTab = true;
    std::uint32_t previewPixels = 0;
    std::shared_ptr<DialogResult> dialog = std::make_shared<DialogResult>();

    void Select(int index, std::uint32_t offset = 0, std::uint32_t firstString = 0)
    {
        const bool changed = selected != index;
        selected = index;
        archive.ReadDetail(static_cast<std::size_t>(index), offset, detail, detailError, firstString);
        if (changed)
            SelectPreview();
    }
    void SelectPreview()
    {
        selectPreviewTab = true;
        SDL_DestroyTexture(previewTexture);
        previewTexture = nullptr;
        previewReady = previewDirty = false;
        previewPixels = 0;
        previewError.clear();
        action = cel = 0;
        previewView = {};
        if (selected < 0)
            return;
        const auto id = archive.Chunks()[selected].id;
        const auto type = Inspector::TypeLabel(id.type);
        previewSupported = type == "TMPL" || type == "BMDL" || type == "ACTR" || type == "SCEN" || type == "BKGD";
        if (!previewSupported)
            return;
        if (!resourcesPath[0])
        {
            previewError = "Set the path to 3dmovie.chk below to load the palette and shading resources.";
            return;
        }
        ThreeMM::Error failure;
        if (previewArchive != archive.Path() || previewResources != resourcesPath || previewContent != contentPath)
        {
            if (!preview.Open(archive.Path(), resourcesPath, failure))
            {
                previewArchive.clear();
                previewError = failure.message;
                return;
            }
            previewArchive = archive.Path();
            previewResources = resourcesPath;
            previewContent = contentPath;
        }
        if (type == "SCEN" || type == "ACTR" || type == "TMPL")
            for (const auto name : {"tmpls.3cn", "mtrls.3cn", "bkgds.3cn", "tdfs.3cn"})
            {
                const std::string sourcePath = std::string(contentPath) + "/" + name;
                SDL_PathInfo info;
                if (SDL_GetPathInfo(sourcePath.c_str(), &info) && info.type == SDL_PATHTYPE_FILE &&
                    !preview.AddContent(1, sourcePath, failure))
                {
                    previewArchive.clear();
                    previewError = failure.message;
                    return;
                }
            }
        previewReady = preview.Select(id, failure);
        previewDirty = previewReady;
        if (!previewReady)
            previewError = failure.message;
        else
        {
            sceneFrame = preview.Info().currentFrame;
            if (preview.Info().cameraCount)
            {
                previewView.width = 640;
                previewView.height = 360;
            }
        }
    }
    void PreviewPanel()
    {
        if (!previewSupported)
            return;
        if (previewReady)
        {
            const auto &info = preview.Info();
            ImGui::TextDisabled("%s / %u parts / %u triangles",
                                info.scene  ? "Scene"
                                : info.prop ? "Prop"
                                            : "Asset",
                                info.parts, info.triangles);
            if (info.timeline && ImGui::SliderInt("Frame", &sceneFrame, info.firstFrame, info.lastFrame))
            {
                ThreeMM::Error failure;
                previewDirty = preview.SetFrame(sceneFrame, failure);
                previewError = failure.message;
            }
            if (info.cameraCount)
            {
                int camera = static_cast<int>(info.currentCamera);
                if (ImGui::SliderInt("Camera", &camera, 0, static_cast<int>(info.cameraCount) - 1))
                {
                    ThreeMM::Error failure;
                    previewDirty = preview.SetCamera(camera, failure);
                    previewError = failure.message;
                    previewView.authoredCamera = true;
                }
                if (info.timeline && ImGui::Checkbox("Authored camera and background", &previewView.authoredCamera))
                    previewDirty = true;
                ImGui::TextDisabled("Backgrounds are prerendered; orbit shows actor geometry.");
            }
            if (!info.actions.empty())
            {
                ImGui::SetNextItemWidth(180);
                if (ImGui::BeginCombo("Action", info.actions[action].name.c_str()))
                {
                    for (std::size_t i = 0; i < info.actions.size(); ++i)
                        if (ImGui::Selectable(info.actions[i].name.c_str(), action == static_cast<int>(i)))
                        {
                            action = static_cast<int>(i);
                            cel = 0;
                            ThreeMM::Error failure;
                            previewDirty = preview.SetPose(action, cel, failure);
                            previewError = failure.message;
                        }
                    ImGui::EndCombo();
                }
                const int lastCel = static_cast<int>(info.actions[action].celCount) - 1;
                if (ImGui::SliderInt("Cel", &cel, 0, lastCel))
                {
                    ThreeMM::Error failure;
                    previewDirty = preview.SetPose(action, cel, failure);
                    previewError = failure.message;
                }
            }
            if (ImGui::Button("Reset view"))
            {
                previewView = {};
                if (info.cameraCount)
                {
                    previewView.width = 640;
                    previewView.height = 360;
                }
                previewDirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled(info.cameraCount && !info.timeline ? "Choose a stored camera view"
                                                                   : "Drag to orbit / wheel to zoom");
            if (previewDirty)
            {
                previewDirty = false;
                ThreeMM::PreviewImage pixels;
                ThreeMM::Error failure;
                if (preview.Render(previewView, pixels, failure))
                {
                    SDL_DestroyTexture(previewTexture);
                    previewTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                                       pixels.width, pixels.height);
                    if (!previewTexture ||
                        !SDL_UpdateTexture(previewTexture, nullptr, pixels.rgba.data(), pixels.width * 4))
                    {
                        SDL_DestroyTexture(previewTexture);
                        previewTexture = nullptr;
                        previewError = SDL_GetError();
                    }
                    else
                    {
                        SDL_SetTextureBlendMode(previewTexture, SDL_BLENDMODE_BLEND);
                        previewPixels = pixels.coveredPixels;
                        previewError.clear();
                    }
                }
                else
                    previewError = failure.message;
            }
            if (previewTexture)
            {
                const float scale = ImGui::GetStyle().FontScaleDpi;
                const float height =
                    std::max(140.f * scale, std::min(420.f * scale, ImGui::GetContentRegionAvail().y - 55.f * scale));
                const float width =
                    std::min(ImGui::GetContentRegionAvail().x, height * previewView.width / previewView.height);
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(previewTexture)),
                             ImVec2(width, width * previewView.height / previewView.width));
                if (ImGui::IsItemHovered())
                {
                    const auto &io = ImGui::GetIO();
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && (info.timeline || !info.cameraCount))
                    {
                        previewView.authoredCamera = false;
                        previewView.yaw += io.MouseDelta.x * .5f;
                        previewView.pitch = std::max(-85.f, std::min(85.f, previewView.pitch + io.MouseDelta.y * .5f));
                        previewDirty = true;
                    }
                    if (io.MouseWheel && (info.timeline || !info.cameraCount))
                    {
                        previewView.authoredCamera = false;
                        previewView.zoom =
                            std::max(.1f, std::min(10.f, previewView.zoom * (io.MouseWheel > 0 ? 1.15f : .87f)));
                        previewDirty = true;
                    }
                }
            }
        }
        if (!previewError.empty())
            ImGui::TextWrapped("%s", previewError.c_str());
        if (ImGui::CollapsingHeader("Resource locations", previewError.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(-100);
            const bool entered = ImGui::InputTextWithHint("##resources", "Path to 3dmovie.chk", resourcesPath,
                                                          sizeof(resourcesPath), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Resources") || entered)
                SelectPreview();
            const auto type = Inspector::TypeLabel(archive.Chunks()[selected].id.type);
            if (type == "SCEN" || type == "ACTR" || type == "TMPL")
            {
                ImGui::SetNextItemWidth(-100);
                const bool changed =
                    ImGui::InputTextWithHint("##content", "Original source 1 content folder", contentPath,
                                             sizeof(contentPath), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if (ImGui::Button("Content") || changed)
                    SelectPreview();
            }
        }
        ImGui::Separator();
    }
    void Strings()
    {
        if (!detail.strings.present)
            return;
        if (!ImGui::CollapsingHeader("String entries", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        if (!detail.strings.error.empty())
        {
            ImGui::TextWrapped("%s", detail.strings.error.c_str());
            return;
        }
        ImGui::TextDisabled("%u entries / %u extra bytes per entry", detail.strings.totalEntries,
                            detail.strings.extraBytes);
        ImGui::BeginDisabled(detail.strings.firstEntry == 0);
        if (ImGui::SmallButton("< Previous entries"))
            Select(selected, detail.offset, detail.strings.firstEntry >= 128 ? detail.strings.firstEntry - 128 : 0);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(detail.strings.firstEntry + detail.strings.entries.size() >= detail.strings.totalEntries);
        if (ImGui::SmallButton("Next entries >"))
            Select(selected, detail.offset, detail.strings.firstEntry + 128);
        ImGui::EndDisabled();
        if (detail.strings.entries.empty())
        {
            ImGui::TextDisabled("No string entries.");
            return;
        }
        const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        const float height = ImGui::GetTextLineHeightWithSpacing() *
                             static_cast<float>(std::min<std::size_t>(9, detail.strings.entries.size() + 1));
        if (ImGui::BeginTable("strings", 3, flags, ImVec2(0, height)))
        {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 48);
            ImGui::TableSetupColumn("String", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Extra bytes (hex)", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(detail.strings.entries.size()));
            while (clip.Step())
                for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
                {
                    const auto &entry = detail.strings.entries[row];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", entry.index);
                    ImGui::TableNextColumn();
                    const auto text = DisplayString(entry.text);
                    ImGui::TextUnformatted(text.empty() ? "\"\"" : text.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", text.empty() ? "Empty string" : text.c_str());
                    ImGui::TableNextColumn();
                    std::string hex;
                    for (std::size_t i = 0; i < std::min<std::size_t>(64, entry.extra.size()); ++i)
                    {
                        char byte[4];
                        std::snprintf(byte, sizeof(byte), "%02x ", entry.extra[i]);
                        hex += byte;
                    }
                    if (entry.extra.size() > 64)
                        hex += "... (first 64 bytes)";
                    ImGui::TextUnformatted(hex.empty() ? "--" : hex.c_str());
                    if (ImGui::IsItemHovered() && !hex.empty())
                        ImGui::SetTooltip("%s", hex.c_str());
                }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Control characters are escaped. Extra data retains its stored byte order.");
    }
    void Browse(SDL_Window *window)
    {
        {
            std::lock_guard<std::mutex> lock(dialog->mutex);
            if (dialog->pending)
                return;
            dialog->pending = true;
        }
        static const SDL_DialogFileFilter filters[] = {{"3D Movie Maker / chunky files", "3mm;3cn;chk;chn;3th"},
                                                       {"All files", "*"}};
        SDL_ShowOpenFileDialog(FileChosen, new std::shared_ptr<DialogResult>(dialog), window, filters, 2, nullptr,
                               false);
    }
    void PollDialog()
    {
        std::string chosen, message;
        {
            std::lock_guard<std::mutex> lock(dialog->mutex);
            if (!dialog->ready)
                return;
            dialog->ready = false;
            chosen.swap(dialog->path);
            message.swap(dialog->error);
        }
        if (!message.empty())
            error = message;
        if (!chosen.empty())
            Open(chosen.c_str());
    }
    void ChunkList()
    {
        ImGui::TextColored(accent, "ARCHIVE CONTENTS");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Filter by type, ID or name...", filter, sizeof(filter));
        ImGuiTextFilter query;
        SDL_strlcpy(query.InputBuf, filter, sizeof(query.InputBuf));
        query.Build();
        std::vector<int> visible;
        const auto &chunks = archive.Chunks();
        for (std::size_t i = 0; i < chunks.size(); ++i)
        {
            const auto label = Inspector::TypeDisplayName(chunks[i].id.type) + " " + Inspector::Identity(chunks[i].id) +
                               " " + chunks[i].name;
            if (query.PassFilter(label.c_str()))
                visible.push_back(static_cast<int>(i));
        }
        ImGui::TextDisabled("%zu of %zu chunks", visible.size(), chunks.size());
        const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("chunks", 4, flags))
        {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 225);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 84);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(visible.size()));
            while (clip.Step())
                for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
                {
                    const int index = visible[row];
                    const auto &chunk = chunks[index];
                    ImGui::PushID(index);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Selectable(Inspector::TypeDisplayName(chunk.id.type).c_str(), selected == index,
                                          ImGuiSelectableFlags_SpanAllColumns))
                        Select(index);
                    ImGui::TableNextColumn();
                    ImGui::Text("%08x", static_cast<unsigned>(chunk.id.number));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(chunk.name.empty() ? "--" : chunk.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", static_cast<unsigned>(chunk.storedBytes));
                    ImGui::PopID();
                }
            ImGui::EndTable();
        }
    }
    void ChunkDetails()
    {
        if (selected < 0)
        {
            ImGui::TextColored(accent, "CHUNK DETAILS");
            ImGui::TextDisabled("Select a chunk to inspect its contents.");
            return;
        }
        // Copy: following a child may change the current selection this frame.
        const auto chunk = archive.Chunks()[selected];
        ImGui::TextColored(accent, "%s", Inspector::TypeDisplayName(chunk.id.type).c_str());
        ImGui::TextDisabled("%s", Inspector::Identity(chunk.id).c_str());
        ImGui::TextWrapped("%s", chunk.name.empty() ? "Unnamed chunk" : chunk.name.c_str());
        ImGui::TextDisabled("Type %08x  |  %u stored bytes", static_cast<unsigned>(chunk.id.type),
                            static_cast<unsigned>(chunk.storedBytes));
        ImGui::TextDisabled("%s  %s  %s", chunk.packed ? "Packed" : "Uncompressed", chunk.loner ? "/ Loner" : "",
                            chunk.forest ? "/ Forest" : "");
        if (ImGui::Button("Copy chunk identity"))
            ImGui::SetClipboardText(Inspector::Identity(chunk.id).c_str());
        ImGui::Separator();
        if (!detailError.empty())
            ImGui::TextWrapped("%s", detailError.c_str());
        for (const auto &field : detail.fields)
            ImGui::TextWrapped("%s", field.c_str());
        Strings();
        if (ImGui::CollapsingHeader("Child references", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (chunk.children.empty())
                ImGui::TextDisabled("No child references.");
            int follow = -1;
            for (std::size_t i = 0; i < chunk.children.size(); ++i)
            {
                const auto &child = chunk.children[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TextDisabled("%08x", static_cast<unsigned>(child.slot));
                ImGui::SameLine();
                char number[16];
                std::snprintf(number, sizeof(number), "%08x", static_cast<unsigned>(child.target.number));
                const auto label = Inspector::TypeDisplayName(child.target.type) + " / " + number;
                if (ImGui::SmallButton(label.c_str()))
                {
                    const auto &chunks = archive.Chunks();
                    for (std::size_t j = 0; j < chunks.size(); ++j)
                        if (chunks[j].id == child.target)
                            follow = static_cast<int>(j);
                }
                if (!child.present)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted("Missing target");
                }
                ImGui::PopID();
            }
            if (follow >= 0)
            {
                Select(follow);
                return;
            }
        }
        ImGui::Separator();
        ImGui::TextColored(accent, "PAYLOAD / HEX");
        if (!detailError.empty())
            return;
        ImGui::TextDisabled("%u unpacked bytes", static_cast<unsigned>(detail.totalBytes));
        ImGui::BeginDisabled(detail.offset == 0);
        if (ImGui::SmallButton("< Previous"))
            Select(selected, detail.offset >= 4096 ? detail.offset - 4096 : 0, detail.strings.firstEntry);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(detail.offset + detail.bytes.size() >= detail.totalBytes);
        if (ImGui::SmallButton("Next >"))
            Select(selected, detail.offset + 4096, detail.strings.firstEntry);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%08x - %08x", static_cast<unsigned>(detail.offset),
                            static_cast<unsigned>(detail.offset + detail.bytes.size()));
        ImGui::BeginChild("hex", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        const int lines = static_cast<int>((detail.bytes.size() + 15) / 16);
        ImGuiListClipper clip;
        clip.Begin(lines);
        while (clip.Step())
            for (int line = clip.DisplayStart; line < clip.DisplayEnd; ++line)
            {
                char text[96], ascii[17] = {};
                int count =
                    std::snprintf(text, sizeof(text), "%08x  ", static_cast<unsigned>(detail.offset + line * 16));
                for (int i = 0; i < 16; ++i)
                {
                    const auto index = static_cast<std::size_t>(line * 16 + i);
                    if (index < detail.bytes.size())
                    {
                        const auto byte = detail.bytes[index];
                        count +=
                            std::snprintf(text + count, sizeof(text) - count, "%02x ", static_cast<unsigned>(byte));
                        ascii[i] = byte >= 32 && byte < 127 ? static_cast<char>(byte) : '.';
                    }
                    else
                        count += std::snprintf(text + count, sizeof(text) - count, "   ");
                }
                std::snprintf(text + count, sizeof(text) - count, " |%s|", ascii);
                ImGui::TextUnformatted(text);
            }
        ImGui::EndChild();
    }
    void Details()
    {
        if (selected < 0 || !previewSupported)
        {
            ChunkDetails();
            return;
        }
        const auto &chunk = archive.Chunks()[selected];
        ImGui::TextColored(accent, "%s",
                           chunk.name.empty() ? Inspector::TypeDisplayName(chunk.id.type).c_str() : chunk.name.c_str());
        ImGui::TextDisabled("%s", Inspector::Identity(chunk.id).c_str());
        if (ImGui::BeginTabBar("asset-tabs"))
        {
            if (ImGui::BeginTabItem("3D view", nullptr, selectPreviewTab ? ImGuiTabItemFlags_SetSelected : 0))
            {
                PreviewPanel();
                ImGui::EndTabItem();
            }
            selectPreviewTab = false;
            if (ImGui::BeginTabItem("Chunk data"))
            {
                ChunkDetails();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

  public:
    explicit App(SDL_Renderer *value) : renderer(value)
    {
    }
    ~App()
    {
        SDL_DestroyTexture(previewTexture);
    }
    void SetResources(const char *value)
    {
        SDL_strlcpy(resourcesPath, value, sizeof(resourcesPath));
    }
    void SetContent(const char *value)
    {
        SDL_strlcpy(contentPath, value, sizeof(contentPath));
    }
    bool SelectAsset(const char *type, std::uint32_t number)
    {
        for (std::size_t i = 0; i < archive.Chunks().size(); ++i)
            if (Inspector::TypeLabel(archive.Chunks()[i].id.type) == type && archive.Chunks()[i].id.number == number)
            {
                Select(static_cast<int>(i));
                return previewReady;
            }
        return false;
    }
    bool HasRenderedPreview() const
    {
        return previewPixels > 0 && previewError.empty();
    }
    bool Open(const char *filename)
    {
        const std::string input(filename); // May alias path.
        SDL_strlcpy(path, input.c_str(), sizeof(path));
        if (!archive.Open(input, error))
            return false;
        preview.Close();
        previewArchive.clear();
        SDL_DestroyTexture(previewTexture);
        previewTexture = nullptr;
        previewSupported = previewReady = false;
        if (!contentPath[0])
        {
            for (const auto &candidate :
                 {std::string("content-files"), std::string(SDL_GetBasePath()) + "../../../../content-files"})
            {
                SDL_PathInfo info;
                if (SDL_GetPathInfo(candidate.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY)
                {
                    SetContent(candidate.c_str());
                    break;
                }
            }
        }
        if (!resourcesPath[0])
        {
            const auto slash = input.find_last_of("/\\");
            const std::vector<std::string> candidates = {
                input.substr(0, slash == std::string::npos ? 0 : slash + 1) + "3dmovie.chk", "3dmovie.chk",
                "dist/Microsoft Kids/3D Movie Maker/3dmovie.chk",
                std::string(SDL_GetBasePath()) + "../../../../dist/Microsoft Kids/3D Movie Maker/3dmovie.chk"};
            for (const auto &candidate : candidates)
            {
                SDL_PathInfo info;
                if (SDL_GetPathInfo(candidate.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
                {
                    SetResources(candidate.c_str());
                    break;
                }
            }
        }
        filter[0] = 0;
        selected = -1;
        detail = {};
        detailError.clear();
        if (!archive.Chunks().empty())
            Select(0);
        return true;
    }
    void Draw(SDL_Window *window)
    {
        PollDialog();
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("3mm Inspector", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(accent, "3MM INSPECTOR");
        ImGui::SameLine();
        ImGui::TextDisabled("/ archive explorer");
        ImGui::SameLine(ImGui::GetWindowWidth() - 120 * ImGui::GetStyle().FontScaleDpi);
        ImGui::TextDisabled("READ ONLY");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(
            std::max(80.0f, ImGui::GetContentRegionAvail().x - 205 * ImGui::GetStyle().FontScaleDpi));
        const bool enter = ImGui::InputTextWithHint("##path", "Open a .3mm, .3cn or .chk file...", path, sizeof(path),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Open") || enter)
            Open(path);
        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
            Browse(window);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
            Browse(window);
        if (!error.empty())
            ImGui::TextColored(ImVec4(1, .65f, .45f, 1), "%s", error.c_str());
        ImGui::TextDisabled("%s",
                            archive.Path().empty() ? "Drop a file anywhere to get started." : archive.Path().c_str());
        ImGui::Separator();
        const float height = ImGui::GetContentRegionAvail().y;
        if (archive.Path().empty())
        {
            ImGui::Dummy(ImVec2(0, height * .25f));
            ImGui::TextColored(accent, "Explore a movie from the inside.");
            ImGui::TextWrapped("Browse chunks, follow their references, and inspect the original payload bytes.");
            ImGui::TextDisabled("Open a file above, press Ctrl+O, or drag it into this window.");
        }
        else if (ImGui::BeginTable("panes", 2, ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Archive", ImGuiTableColumnFlags_WidthStretch, .48f);
            ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch, .52f);
            ImGui::TableNextColumn();
            ImGui::BeginChild("list", ImVec2(0, height - 8), ImGuiChildFlags_Borders);
            ChunkList();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("detail", ImVec2(0, height - 8), ImGuiChildFlags_Borders);
            Details();
            ImGui::EndChild();
            ImGui::EndTable();
        }
        ImGui::End();
    }
};
} // namespace

int main(int argc, char **argv)
{
    bool smoke = false;
    const char *input = nullptr, *screenshot = nullptr, *resources = nullptr, *selection = nullptr, *content = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--smoke-test"))
            smoke = true;
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc)
            screenshot = argv[++i];
        else if (!std::strcmp(argv[i], "--resources") && i + 1 < argc)
            resources = argv[++i];
        else if (!std::strcmp(argv[i], "--select") && i + 1 < argc)
            selection = argv[++i];
        else if (!std::strcmp(argv[i], "--content-dir") && i + 1 < argc)
            content = argv[++i];
        else if (argv[i][0] == '-' || input)
        {
            std::fputs("usage: 3mm-inspector [--smoke-test] [--screenshot image.bmp] [--resources 3dmovie.chk] "
                       "[--content-dir folder] [--select TMPL:2010] [archive]\n",
                       stderr);
            return 2;
        }
        else
            input = argv[i];
    }
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0)
        scale = 1;
    SDL_Window *window =
        SDL_CreateWindow("3mm Inspector", static_cast<int>(1280 * scale), static_cast<int>(820 * scale),
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (smoke ? SDL_WINDOW_HIDDEN : 0));
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer)
    {
        SDL_Log("Create inspector: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 840, 580);
    SDL_SetRenderVSync(renderer, 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr;
    Style(scale);
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) || !ImGui_ImplSDLRenderer3_Init(renderer))
    {
        SDL_Log("Cannot initialize ImGui backends");
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    auto app = std::unique_ptr<App>(new App(renderer));
    if (resources)
        app->SetResources(resources);
    if (content)
        app->SetContent(content);
    int result = input && !app->Open(input) && smoke ? 1 : 0;
    if (selection)
    {
        const std::string value(selection);
        char *end = nullptr;
        const auto number = value.size() > 5 ? std::strtoul(value.c_str() + 5, &end, 16) : 0;
        if (value.size() < 6 || value[4] != ':' || !end || *end || number > UINT32_MAX ||
            !app->SelectAsset(value.substr(0, 4).c_str(), static_cast<std::uint32_t>(number)))
            result = 1;
    }
    bool done = result != 0;
    int frames = 0;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)))
                done = true;
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data)
                app->Open(event.drop.data);
        }
        if (!smoke && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED))
        {
            SDL_Delay(30);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app->Draw(window);
        ImGui::Render();
        SDL_SetRenderScale(renderer, ImGui::GetIO().DisplayFramebufferScale.x,
                           ImGui::GetIO().DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 18, 22, 26, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        // Allow nested child windows and the dynamic font atlas to settle.
        if (++frames == 8 && screenshot)
        {
            SDL_Surface *pixels = SDL_RenderReadPixels(renderer, nullptr);
            if (!pixels || !SDL_SaveBMP(pixels, screenshot))
            {
                SDL_Log("Screenshot: %s", SDL_GetError());
                result = 1;
            }
            SDL_DestroySurface(pixels);
        }
        SDL_RenderPresent(renderer);
        if (smoke && frames >= 8)
            done = true;
    }
    if (selection && !app->HasRenderedPreview())
        result = 1;
    app.reset();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
