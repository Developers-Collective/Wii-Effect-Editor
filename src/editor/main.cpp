#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <aurora/gfx.hpp>
#include <dolphin/gx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <png.h>
#include "logo.h"
#include <misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include "preview_controls.h"
#include "orbit_camera.h"
#include "preview_view.h"
#include "document.h"
#include "../codec/form.h"
#include "../codec/animation.h"
#include "../runtime/archive.h"
#include "../runtime/engine.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <iostream>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

using Json = nlohmann::json;

namespace {
float uiScale = 1.f;
float lastDisplayScale = 0.f, lastPixelScale = 0.f;
struct Application {
    breff::DocumentService documents;
    std::future<Json> response;
    Json state = {{"loaded", false}};
    Json draft;
    Json form;
    uint64_t formRevision=~uint64_t(0);
    std::string search, error, input, raw, breffPath, breftPath, filePath;
    bool busy = false, dirtyDraft = false, quitting = false, disconnected = false;
    bool rawDirty = false, closeRequested = false;
    int nextId = 1;
    std::deque<Json> queue;
    std::string activeOperation;
    std::mutex dialogMutex;
    std::string dialogPath;
    int dialogTarget = 0, dialogResult = 0;
    bool requestMissingTextures=false;
    std::string missingTextureStatus;
    SDL_Window* window = nullptr;
    PreviewControls preview;
    OrbitCamera camera;
    PreviewRect previewRect;
    breff::Archive archive;
    breff::Engine engine;
    std::string playingEffect;
    int playingGeneration=-1;
    uint64_t playingRestart=0;
    double simulationTime=0;
    int loadedGeneration = -1;
    std::string previewError;
    bool loop = true, showGrid = true, showAxes = true, textureMode = false, editingRaw = false;
    int selectResourceTab = -1;
    std::string resourceName, selectedTexture, replacementTexture;
    uint64_t editRevision=0, sentRevision=0, failedRevision=~uint64_t(0);
    double lastSubmission=0;
    struct Image { wgpu::Texture texture; wgpu::TextureView view; };
    std::map<std::string,Image> images;

    const Json* texture(const std::string& name) const {
        if (!state.contains("textures")) return nullptr;
        for (const auto& item:state["textures"]) if (item["name"]==name) return &item;
        return nullptr;
    }
    ImTextureID image(const Json& item) {
        const auto path=item.value("image","");
        if (path.empty()) return 0;
        if (auto it=images.find(path);it!=images.end()) return reinterpret_cast<ImTextureID>(it->second.view.Get());
        const uint32_t w=item.at("width"),h=item.at("height");
        std::ifstream stream(std::filesystem::u8path(path),std::ios::binary);
        std::vector<uint8_t> pixels(size_t(w)*h*4);
        if (!stream.read(reinterpret_cast<char*>(pixels.data()),pixels.size())) return 0;
        wgpu::TextureDescriptor descriptor{};
        descriptor.size={w,h,1}; descriptor.format=wgpu::TextureFormat::RGBA8Unorm;
        descriptor.usage=wgpu::TextureUsage::TextureBinding|wgpu::TextureUsage::CopyDst;
        Image result; result.texture=aurora::gfx::device().CreateTexture(&descriptor);
        wgpu::TexelCopyTextureInfo destination{}; destination.texture=result.texture;
        wgpu::TexelCopyBufferLayout layout{}; layout.bytesPerRow=w*4; layout.rowsPerImage=h;
        aurora::gfx::queue().WriteTexture(&destination,pixels.data(),pixels.size(),&layout,&descriptor.size);
        result.view=result.texture.CreateView();
        auto id=reinterpret_cast<ImTextureID>(result.view.Get());
        images.emplace(path,std::move(result)); return id;
    }

    void enqueue(Json request) { queue.push_back(std::move(request)); }
    void flush() {
        if (busy || queue.empty()) return;
        Json request = std::move(queue.front());
        queue.pop_front();
        request["id"] = nextId++;
        activeOperation = request.at("op").get<std::string>();
        if (activeOperation=="replace") sentRevision=editRevision;
        response=std::async(std::launch::async,[this,request=std::move(request)]() {
            return Json(documents.handle(breff::codec::Json(request)));
        });
        busy = true;
    }
    bool loaded() const { return state.value("loaded", false); }
    bool unsaved() const { return dirtyDraft || rawDirty || state.value("dirty", false); }
    std::string selected() const {
        const auto it = state.find("selected");
        return it != state.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    void apply() {
        if (!dirtyDraft || busy || rawDirty || failedRevision==editRevision) return;
        enqueue({{"op", "replace"}, {"name", selected()}, {"effect", draft}});
        lastSubmission=ImGui::GetTime();
        flush();
    }
    void applyThen(Json request) {
        if (rawDirty || failedRevision==editRevision) { error = "Correct the invalid edit before continuing."; closeRequested = false; return; }
        apply();
        enqueue(std::move(request));
    }
    void receive(const Json& response) {
        busy = false;
        if (!response.value("ok", false)) {
            if (activeOperation=="replace") failedRevision=sentRevision;
            error = response.value("error", "Backend request failed");
            queue.clear();
            closeRequested = false;
            return;
        }
        const bool keepDraft=activeOperation=="preview_texture_folder" ||
            (activeOperation=="replace" && editRevision!=sentRevision);
        state = response.at("data");
        if(activeOperation=="open") {
            requestMissingTextures=!state.value("missingTextures",Json::array()).empty();
            missingTextureStatus.clear();
        }
        if(activeOperation=="preview_texture_folder") {
            missingTextureStatus=std::to_string(state.value("previewTexturesMatched",size_t(0)))+" matching PNGs loaded for rendering.";
            requestMissingTextures=true;
        }
        if (state.contains("inspectTexture")) { selectedTexture=state["inspectTexture"].get<std::string>(); textureMode=true; selectResourceTab=1; }
        if (loaded() && loadedGeneration != state.value("generation",0)) {
            try {
                engine.reset();
                archive.load(std::filesystem::u8path(state.at("snapshot").get<std::string>()),
                             std::filesystem::u8path(state.at("texturePath").get<std::string>()));
                loadedGeneration=state.value("generation",0);
                previewError.clear();
            } catch (const std::exception& e) { previewError=e.what(); }
        }
        if (!keepDraft) {
            draft = state.value("effect", Json());
            if (!editingRaw || activeOperation!="replace") raw = draft.is_null() ? "" : draft.dump(2);
            dirtyDraft = rawDirty = false;
            formRevision=~uint64_t(0);
        }
        if (loaded() && !texture(selectedTexture)) selectedTexture=state["textures"].empty() ? "" : state["textures"][0].value("name","");
        error.clear();
        if (closeRequested && activeOperation == "save") quitting = true;
        SDL_SetWindowTitle(window, "Effect Editor");
    }
    void pollInput() {
        if(response.valid() && response.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try {receive(response.get());}
            catch(const std::exception& e) {error=e.what();busy=false;queue.clear();}
        }
    }

    static void fileSelected(void* userdata, const char* const* files, int) {
        auto* app = static_cast<Application*>(userdata);
        std::lock_guard lock(app->dialogMutex);
        if (files && files[0]) { app->dialogPath = files[0]; app->dialogResult = app->dialogTarget; }
        app->dialogTarget = 0;
    }
    void choose(int target, bool save = false) {
        std::lock_guard lock(dialogMutex);
        if (dialogTarget) return;
        dialogTarget = target;
        if(target==4)SDL_ShowOpenFolderDialog(fileSelected,this,window,nullptr,false);
        else if (save) SDL_ShowSaveFileDialog(fileSelected, this, window, nullptr, 0, nullptr);
        else SDL_ShowOpenFileDialog(fileSelected, this, window, nullptr, 0, nullptr, false);
    }
    void pollDialog() {
        std::lock_guard lock(dialogMutex);
        if (dialogResult == 1) breffPath = dialogPath;
        if (dialogResult == 2) breftPath = dialogPath;
        if (dialogResult == 3) filePath = dialogPath;
        if (dialogResult == 4) enqueue({{"op","preview_texture_folder"},{"path",dialogPath}});
        if (dialogResult==1 || dialogResult==2) completePair();
        dialogResult = 0;
    }
    void completePair() {
        auto fill=[](const std::string& source,std::string& target,const char* extension) {
            if (source.empty() || !target.empty()) return;
            auto sibling=std::filesystem::u8path(source); sibling.replace_extension(extension);
            std::error_code error;
            if (std::filesystem::is_regular_file(sibling,error)) {
                const auto utf8=sibling.u8string(); target.assign(utf8.begin(),utf8.end());
            }
        };
        fill(breffPath,breftPath,".breft"); fill(breftPath,breffPath,".breff");
    }
};

void style() {
    ImGui::GetStyle() = ImGuiStyle{};
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 5; s.GrabRounding = 5;
    s.FramePadding = ImVec2(8, 6); s.ItemSpacing = ImVec2(8, 7);
    s.WindowPadding = ImVec2(16, 14);
    const ImVec4 accent(194.f/255.f, 0.f, 252.f/255.f, 1.f); // #c200fc
    const ImVec4 muted(.29f, .10f, .36f, 1.f);
    const ImVec4 hovered(.48f, .12f, .59f, 1.f);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(.075f, .065f, .09f, 1);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(.095f, .08f, .115f, 1);
    s.Colors[ImGuiCol_PopupBg] = ImVec4(.12f, .09f, .145f, 1);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(.16f, .12f, .19f, 1);
    for (auto color : {ImGuiCol_Header, ImGuiCol_Button, ImGuiCol_Tab,
                       ImGuiCol_TabDimmedSelected, ImGuiCol_TitleBgActive})
        s.Colors[color] = muted;
    for (auto color : {ImGuiCol_HeaderHovered, ImGuiCol_ButtonHovered,
                       ImGuiCol_FrameBgHovered, ImGuiCol_TabHovered,
                       ImGuiCol_TabSelected, ImGuiCol_SeparatorHovered,
                       ImGuiCol_ResizeGripHovered, ImGuiCol_ScrollbarGrabHovered})
        s.Colors[color] = hovered;
    for (auto color : {ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive,
                       ImGuiCol_ButtonActive, ImGuiCol_HeaderActive, ImGuiCol_FrameBgActive,
                       ImGuiCol_SeparatorActive, ImGuiCol_ResizeGripActive,
                       ImGuiCol_ScrollbarGrabActive, ImGuiCol_TabSelectedOverline,
                       ImGuiCol_TabDimmedSelectedOverline, ImGuiCol_NavCursor})
        s.Colors[color] = accent;
    s.Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, .35f);
    s.Colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, .65f);
    s.Colors[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, .2f);
}

void updateDpi(const AuroraWindowSize& size) {
    float displayScale = size.scale > 0.f ? size.scale : 1.f;
    float pixelScale = size.width ? float(size.native_fb_width) / float(size.width) : 1.f;
    if (pixelScale <= 0.f) pixelScale = 1.f;
    if (std::abs(displayScale-lastDisplayScale)<.01f && std::abs(pixelScale-lastPixelScale)<.01f) return;
    lastDisplayScale=displayScale; lastPixelScale=pixelScale;
    // Windows window coordinates are pixels; macOS may already use points.
    // Dividing by framebuffer density avoids applying Retina scaling twice.
    uiScale=displayScale/pixelScale;
    style();
    ImGui::GetStyle().ScaleAllSizes(uiScale);
    auto& io=ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig font{};
    font.SizePixels=16.f*displayScale;
    font.OversampleH=3;
    font.OversampleV=2;
    font.PixelSnapH=false;
    std::filesystem::path fontPath;
#ifdef _WIN32
    char windowsDirectory[MAX_PATH]{};
    if (GetWindowsDirectoryA(windowsDirectory, MAX_PATH))
        fontPath=std::filesystem::path(windowsDirectory)/"Fonts"/"arial.ttf";
#elif defined(__APPLE__)
    fontPath="/System/Library/Fonts/Supplemental/Arial.ttf";
#else
    fontPath="/usr/share/fonts/truetype/msttcorefonts/Arial.ttf";
#endif
    io.FontDefault=nullptr;
    if (std::filesystem::exists(fontPath))
        io.FontDefault=io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(),font.SizePixels,&font);
    if (!io.FontDefault) {
        std::fprintf(stderr,"Arial unavailable; using fallback UI font.\n");
        io.FontDefault=io.Fonts->AddFontDefault(&font);
    }
    io.FontGlobalScale=1.f/pixelScale;
    std::fprintf(stderr,"UI display scale %.2f, coordinate scale %.2f\n",displayScale,uiScale);
}

void updateDpi(SDL_Window* window) {
    int w=0,h=0,pw=0,ph=0;
    SDL_GetWindowSize(window,&w,&h); SDL_GetWindowSizeInPixels(window,&pw,&ph);
    AuroraWindowSize size{};
    size.width=w; size.height=h; size.native_fb_width=pw; size.native_fb_height=ph;
    size.scale=SDL_GetWindowDisplayScale(window);
    updateDpi(size);
}

void placeWindow(SDL_Window* window) {
    SDL_Rect area{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &area)) return;
    int top=0,left=0,bottom=0,right=0;
    SDL_GetWindowBordersSize(window,&top,&left,&bottom,&right);
    const int margin=std::max(8,int(16*uiScale));
    const int width=std::max(1,std::min(int(1500*uiScale),area.w-left-right-2*margin));
    const int height=std::max(1,std::min(int(950*uiScale),area.h-top-bottom-2*margin));
    SDL_SetWindowSize(window,width,height);
    SDL_SetWindowPosition(window,area.x+(area.w-width-left-right)/2+left,
                          area.y+(area.h-height-top-bottom)/2+top);
    SDL_SyncWindow(window);
#ifdef _WIN32
    // Position the complete native frame, including the caption. This also
    // handles Windows' invisible resize borders and monitors at negative x/y.
    auto hwnd=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    MONITORINFO monitor{sizeof(MONITORINFO)};
    RECT frame{};
    if (hwnd && GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor) && GetWindowRect(hwnd,&frame)) {
        const auto& work=monitor.rcWork;
        int w=std::min<int>(frame.right-frame.left,work.right-work.left-2*margin);
        int h=std::min<int>(frame.bottom-frame.top,work.bottom-work.top-2*margin);
        SetWindowPos(hwnd,nullptr,work.left+(work.right-work.left-w)/2,
                     work.top+(work.bottom-work.top-h)/2,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
    }
#endif
}

std::string fieldPath(const std::string& path,const std::string& key) {
    std::string result=path+"/";
    for (char c:key) { if (c=='~') result+="~0"; else if (c=='/') result+="~1"; else result+=c; }
    return result;
}

void textureImage(Application& app,const std::string& name,float size,bool enlarge=true) {
    const auto* item=app.texture(name);
    if (!item) return;
    const auto id=app.image(*item);
    if (!id) { ImGui::TextWrapped("%s",item->value("error","Image unavailable").c_str()); return; }
    float w=item->at("width"),h=item->at("height");
    const float scale=size/std::max(w,h);
    ImVec2 extent(w*scale,h*scale),pos=ImGui::GetCursorScreenPos();
    auto* draw=ImGui::GetWindowDrawList();
    for (float y=0;y<extent.y;y+=12*uiScale) for (float x=0;x<extent.x;x+=12*uiScale) {
        const auto color=(int(x/(12*uiScale))+int(y/(12*uiScale)))%2 ? IM_COL32(70,65,75,255) : IM_COL32(40,35,45,255);
        draw->AddRectFilled(ImVec2(pos.x+x,pos.y+y),ImVec2(pos.x+std::min(x+12*uiScale,extent.x),pos.y+std::min(y+12*uiScale,extent.y)),color);
    }
    ImGui::Image(id,extent);
    if (enlarge && ImGui::IsItemClicked()) ImGui::OpenPopup("Texture image");
    if (enlarge && ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%d x %d)\nClick to enlarge",name.c_str(),int(w),int(h));
    if (enlarge && ImGui::BeginPopup("Texture image")) {
        ImGui::Text("%s  (%d x %d)",name.c_str(),int(w),int(h));
        textureImage(app,name,std::min(512.f*uiScale,ImGui::GetMainViewport()->WorkSize.y*.65f),false);
        ImGui::EndPopup();
    }
}

bool editValue(Application& app,const std::string& name, Json& value, bool& commit, const Json& choices, const std::string& path,const std::string& widget) {
    bool changed = false;
    ImGui::PushID(name.c_str());
    const auto field = choices.find(path);
    const bool textureSlot=path=="/particle/texture1" || path=="/particle/texture2" || path=="/particle/textureInd";
    if (textureSlot && value.is_object()) {
        const std::string current=value.value("name",std::string());
        ImGui::SetNextItemWidth(std::max(120.f*uiScale,ImGui::GetContentRegionAvail().x*.6f));
        if(ImGui::BeginCombo(name.c_str(),current.empty()?"None":current.c_str())) {
            auto selectTexture=[&](const std::string& selected) {
                // A binding is optional; its transform and particle offsets are
                // fixed fields in the binary and survive binding changes.
                value["name"]=selected;
                const char* flag=path=="/particle/texture1"?"useTexture1":path=="/particle/texture2"?"useTexture2":"useIndirectTexture";
                app.draft["emitter"]["drawFlags"][flag]=!selected.empty();
                changed=commit=true;
            };
            if(ImGui::Selectable("None",current.empty())) selectTexture("");
            if(app.state.contains("textures")) for(const auto& texture:app.state["textures"]) {
                const auto candidate=texture.at("name").get<std::string>();
                if(ImGui::Selectable(candidate.c_str(),candidate==current)) selectTexture(candidate);
            }
            ImGui::EndCombo();
        }
        if(!value.value("name",std::string()).empty()) {
            ImGui::SameLine(); textureImage(app,value.value("name",std::string()),48*uiScale);
        }
    } else if (widget=="color") {
        float rgba[4]; const char* keys[]={"r","g","b","a"};
        for (int i=0;i<4;++i) rgba[i]=value[keys[i]].get<float>()/255.f;
        if (ImGui::ColorEdit4(name.c_str(),rgba,ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreviewHalf|ImGuiColorEditFlags_DisplayHex)) {
            for (int i=0;i<4;++i) value[keys[i]]=int(std::clamp(rgba[i],0.f,1.f)*255.f+.5f);
            changed=commit=true;
        }
    } else if (name=="name" && path.starts_with("/animations/") && value.is_string()) {
        ImGui::SetNextItemWidth(std::max(120.f*uiScale,ImGui::GetContentRegionAvail().x*.7f));
        const auto current=value.get<std::string>();
        if(ImGui::BeginCombo(name.c_str(),current.empty()?"None":current.c_str())) {
            if(ImGui::Selectable("None",current.empty())) { value="";changed=commit=true; }
            if(app.state.contains("names")) for(const auto& option:app.state["names"]) {
                const auto& candidate=option.get_ref<const std::string&>();
                if(ImGui::Selectable(candidate.c_str(),current==candidate)) {value=candidate;changed=commit=true;}
                if(current==candidate) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    } else if (field != choices.end() && field->is_object() && field->value("kind", "") == "texture" && value.is_string()) {
        ImGui::SetNextItemWidth(std::max(120.f*uiScale,ImGui::GetContentRegionAvail().x*.7f));
        const auto current=value.get<std::string>();
        if (ImGui::BeginCombo(name.c_str(),current.empty() ? "None" : current.c_str())) {
            for (const auto& option:field->at("options")) {
                const auto text=option.get<std::string>();
                if (ImGui::Selectable(text.empty() ? "None" : text.c_str(),value==option)) { value=option; changed=commit=true; }
            }
            ImGui::EndCombo();
        }
        if (app.texture(value.get<std::string>())) { ImGui::SameLine(); textureImage(app,value.get<std::string>(),48*uiScale); }
    } else if (field != choices.end() && field->is_object() && field->value("kind", "") == "flags" && value.is_object()) {
        std::string summary;
        for (const auto& option : field->at("options")) {
            if (!value.value(option.at("key").get<std::string>(), false)) continue;
            if (!summary.empty()) summary += ", ";
            summary += option.at("label").get<std::string>();
        }
        if (summary.empty()) summary = "None";
        ImGui::SetNextItemWidth(std::max(120.f*uiScale, ImGui::GetContentRegionAvail().x*.46f));
        if (ImGui::BeginCombo(name.c_str(),summary.c_str())) {
            for (const auto& option : field->at("options")) {
                const auto key=option.at("key").get<std::string>();
                bool selected=value.value(key,false);
                if (ImGui::Checkbox(option.at("label").get_ref<const std::string&>().c_str(),&selected)) {
                    value[key]=selected; changed=commit=true;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::SetNextItemWidth(std::max(120.f*uiScale, ImGui::GetContentRegionAvail().x * .46f));
        auto allowed = choices.find(path);
        if (allowed != choices.end() && allowed->is_array()) {
            const auto current = value.is_string() ? value.get<std::string>() : value.dump();
            if (ImGui::BeginCombo(name.c_str(), current.c_str())) {
                for (const auto& option : *allowed) {
                    const auto& text = option.get_ref<const std::string&>();
                    const bool selected = value == option;
                    if (ImGui::Selectable(text.c_str(), selected)) { value=option; changed=commit=true; }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else if (widget=="bool") {
            bool v = value.get<bool>(); changed = ImGui::Checkbox(name.c_str(), &v);
            if (changed) { value = v; commit = true; }
        } else if (widget=="uint") {
            uint64_t v = value.get<uint64_t>(); changed = ImGui::InputScalar(name.c_str(), ImGuiDataType_U64, &v);
            if (changed) value = v;
        } else if (widget=="int") {
            int64_t v = value.get<int64_t>(); changed = ImGui::InputScalar(name.c_str(), ImGuiDataType_S64, &v);
            if (changed) value = v;
        } else if (widget=="float") {
            double v = value.get<double>(); changed = ImGui::InputDouble(name.c_str(), &v, 0, 0, "%.9g");
            if (changed) value = v;
        } else if (widget=="string") {
            auto& v = value.get_ref<std::string&>(); changed = ImGui::InputText(name.c_str(), &v);
        } else ImGui::TextDisabled("%s", name.c_str());
        commit |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ImGui::PopID();
    return changed;
}

bool editForm(Application& app,const Json& node,bool& commit) {
    const std::string kind=node.at("kind");
    if(kind=="root") {bool changed=false;for(const auto& child:node.at("children"))changed|=editForm(app,child,commit);return changed;}
    const std::string path=node.at("path"),label=node.at("label");
    const Json::json_pointer pointer(path);
    if(kind=="value" || kind=="texture") {
        Json value=app.draft.contains(pointer)?app.draft.at(pointer):node.value("default",Json());
        if(value.is_null())value=node.value("default",Json());
        Json choices=Json::object();
        if(node.contains("choices") && !node["choices"].is_null())choices[path]=node["choices"];
        if(path.ends_with("/textureName")) {
            Json options=Json::array({""});for(const auto& item:app.state["textures"])options.push_back(item.at("name"));
            choices[path]={{"kind","texture"},{"options",options}};
        }
        std::string animationPath;Json previous;
        if(path.starts_with("/animations/")) {
            auto end=path.find('/',12);
            if(end!=std::string::npos){animationPath=path.substr(0,end);previous=app.draft.at(Json::json_pointer(animationPath));}
        }
        const std::string widget=node.value("widget",std::string("texture"));
        if(widget=="color") {
            Json complete=node.at("default");
            if(value.is_object())complete.update(value);
            value=std::move(complete);
        }
        bool changed=editValue(app,label,value,commit,choices,path,widget);
        if(changed) {
            if(node.contains("minimum") && node.contains("maximum")) {
                if(widget=="uint")value=std::clamp(value.get<uint64_t>(),node["minimum"].get<uint64_t>(),node["maximum"].get<uint64_t>());
                else value=std::clamp(value.get<int64_t>(),node["minimum"].get<int64_t>(),node["maximum"].get<int64_t>());
            }
            app.draft[pointer]=value;
            if(!animationPath.empty()) {
                breff::codec::Json updated=app.draft.at(Json::json_pointer(animationPath));
                breff::codec::prepareAnimationEdit(updated,breff::codec::Json(previous),breff::codec::Json(app.draft.at("emitter")));
                app.draft[Json::json_pointer(animationPath)]=Json(updated);
            }
        }
        if(kind=="texture") {
            if(ImGui::TreeNode(("Texture settings##"+path).c_str())) {
                const Json fields=breff::codec::recordForm("ParticleTexture",breff::codec::Json(value),path,label);
                for(const auto& field:fields.at("children")) {
                    const std::string childPath=field.at("path");
                    if(childPath!=path+"/name" && childPath!=path+"/rotationOffset" && childPath!=path+"/rotationOffsetRandom")changed|=editForm(app,field,commit);
                }
                ImGui::TreePop();
            }
        }
        return changed;
    }
    ImGui::PushID(path.c_str());bool changed=false;
    if(kind=="optional") {
        const bool present=app.draft.contains(pointer) && !app.draft.at(pointer).empty();
        if(!present) {
            ImGui::TextUnformatted(label.c_str());ImGui::SameLine();
            if(ImGui::SmallButton("Add")) {
                Json value=node.at("default");
                if(value.contains("childType") && app.state.contains("names") && !app.state["names"].empty())value["name"]=app.state["names"][0];
                app.draft[pointer]=value;changed=commit=true;
            }
            ImGui::PopID();return changed;
        }
        if(ImGui::SmallButton("Remove")){app.draft[pointer]=Json::object();changed=commit=true;ImGui::PopID();return changed;}
        ImGui::SameLine();
    }
    if(ImGui::TreeNodeEx("##record",ImGuiTreeNodeFlags_SpanAvailWidth,"%s",label.c_str())) {
        if(kind=="list") {
            const bool fixed=!node.at("fixedLength").is_null();
            const size_t count=app.draft.contains(pointer)?app.draft.at(pointer).size():0;
            size_t remove=count;
            for(size_t i=0;i<node.at("children").size();++i) {
                if(!fixed && i>=count)break;
                if(!fixed){ImGui::PushID(int(i));if(ImGui::SmallButton("Remove"))remove=i;ImGui::PopID();ImGui::SameLine();}
                changed|=editForm(app,node["children"][i],commit);
            }
            if(remove<count){app.draft[pointer].erase(app.draft[pointer].begin()+remove);changed=commit=true;}
            if(!fixed) {
                const bool full=!node.at("maxLength").is_null() && count>=node.at("maxLength").get<size_t>();
                ImGui::BeginDisabled(full);
                if(ImGui::SmallButton("Add item")) {
                    if(!app.draft.contains(pointer))app.draft[pointer]=Json::array();
                    app.draft[pointer].push_back(node.at("default"));changed=commit=true;
                }
                ImGui::EndDisabled();
            }
        } else for(const auto& child:node.at("children"))changed|=editForm(app,child,commit);
        ImGui::TreePop();
    }
    ImGui::PopID();return changed;
}

void dialogs(Application& app) {
    if(app.requestMissingTextures) {ImGui::OpenPopup("Missing textures");app.requestMissingTextures=false;}
    if(ImGui::BeginPopupModal("Missing textures",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto missing=app.state.value("missingTextures",Json::array());
        ImGui::TextUnformatted("Choose a folder containing PNGs named after the missing textures.");
        ImGui::TextDisabled("These images are used only for rendering and are never saved.");
        if(!app.missingTextureStatus.empty())ImGui::TextUnformatted(app.missingTextureStatus.c_str());
        if(!app.error.empty())ImGui::TextWrapped("%s",app.error.c_str());
        if(!missing.empty()) {
            ImGui::BeginChild("Missing texture names",ImVec2(520*uiScale,180*uiScale),ImGuiChildFlags_Borders);
            for(const auto& name:missing)ImGui::TextUnformatted(name.get_ref<const std::string&>().c_str());
            ImGui::EndChild();
        }
        ImGui::BeginDisabled(app.busy);
        if(ImGui::Button("Choose PNG folder"))app.choose(4);
        ImGui::SameLine();
        if(ImGui::Button(missing.empty()?"Continue":"Use black textures"))ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    for (const char* title:{"Add effect","Rename effect","Add texture","Rename texture","Replace texture"}) {
        if (ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::string action=title;
            const bool texture=action.find("texture")!=std::string::npos;
            const bool replace=action=="Replace texture";
            ImGui::SetNextItemWidth(360*uiScale);
            if (!replace) ImGui::InputText("Name",&app.resourceName);
            const bool image=action=="Add texture" || replace;
            if (image) {
                ImGui::InputText("Image file",&app.filePath); ImGui::SameLine();
                if (ImGui::Button("Browse")) app.choose(3);
                ImGui::TextDisabled("PNG, JPEG, BMP, GIF and TIFF images");
                ImGui::TextDisabled("Imported as lossless GX RGBA8 (up to 1024 x 1024).");
            }
            if (ImGui::Button(replace ? "Replace" : "OK")) {
                if (!texture) app.applyThen({{"op",action=="Add effect" ? "effect_add" : "effect_rename"},{"name",app.resourceName}});
                else if (action=="Rename texture") app.applyThen({{"op","texture_rename"},{"name",app.selectedTexture},{"newName",app.resourceName}});
                else app.applyThen({{"op",replace ? "texture_replace" : "texture_add"},{"name",replace ? app.selectedTexture : app.resourceName},{"path",app.filePath}});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    for (const char* title:{"Delete effect","Delete texture"}) {
        if (ImGui::BeginPopupModal(title,nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            bool texture=std::string(title)=="Delete texture";
            ImGui::Text("Delete %s?",(texture ? app.selectedTexture : app.selected()).c_str());
            if (texture) {
                ImGui::TextUnformatted("References will use the selected replacement.");
                if (ImGui::BeginCombo("Replacement",app.replacementTexture.empty() ? "None (clear references)" : app.replacementTexture.c_str())) {
                    if (ImGui::Selectable("None (clear references)",app.replacementTexture.empty())) app.replacementTexture.clear();
                    for (const auto& item:app.state["textures"]) {
                        auto name=item.at("name").get<std::string>();
                        if (name!=app.selectedTexture && ImGui::Selectable(name.c_str(),name==app.replacementTexture)) app.replacementTexture=name;
                    }
                    ImGui::EndCombo();
                }
            } else ImGui::TextUnformatted("Child effects referencing this name will no longer find it.");
            if (ImGui::Button("Delete")) {
                if (texture) app.applyThen({{"op","texture_delete"},{"name",app.selectedTexture},{"replacement",app.replacementTexture}});
                else app.applyThen({{"op","effect_delete"}});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (ImGui::BeginPopupModal("Open BREFF + BREFT", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(500*uiScale); if (ImGui::InputText("BREFF", &app.breffPath)) app.completePair();
        ImGui::SameLine(); if (ImGui::Button("Browse##breff")) app.choose(1);
        ImGui::SetNextItemWidth(500*uiScale); if (ImGui::InputText("BREFT", &app.breftPath)) app.completePair();
        ImGui::SameLine(); if (ImGui::Button("Browse##breft")) app.choose(2);
        ImGui::TextDisabled("Leave BREFT empty to use the matching filename.");
        if (app.unsaved()) ImGui::TextWrapped("Opening another pair discards unsaved changes. Save or cancel first to keep them.");
        if (ImGui::Button("Open pair")) {
            app.enqueue({{"op", "open"}, {"breff", app.breffPath}, {"breft", app.breftPath}});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    for (const auto& [title, operation] : {std::pair{"Save as", "save"}, {"Import JSON", "import"}, {"Export JSON", "export"}}) {
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(550*uiScale); ImGui::InputText("Path", &app.filePath);
            ImGui::SameLine(); if (ImGui::Button("Browse")) app.choose(3, std::string(operation) != "import");
            if (ImGui::Button("Continue")) {
                app.applyThen({{"op", operation}, {"path", app.filePath}});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save your changes before closing?");
        if (ImGui::Button("Save and close")) { app.closeRequested = true; app.applyThen({{"op", "save"}}); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Discard and close")) app.quitting = true;
        ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void drawUI(Application& app, bool requestClose) {
    app.editingRaw=false;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Effect Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    if (requestClose) { if (app.unsaved()) ImGui::OpenPopup("Unsaved changes"); else app.quitting = true; }
    ImGui::BeginDisabled(app.busy || app.disconnected);
    if (ImGui::Button("Open pair")) { app.breffPath.clear(); app.breftPath.clear(); ImGui::OpenPopup("Open BREFF + BREFT"); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!app.loaded());
    if (ImGui::Button("Save")) app.applyThen({{"op", "save"}});
    ImGui::SameLine(); if (ImGui::Button("Save as")) { app.filePath = app.state.value("path", ""); ImGui::OpenPopup("Save as"); }
    ImGui::SameLine(); if (ImGui::Button("Undo")) {
        if (app.dirtyDraft || app.rawDirty) {
            app.draft=app.state.value("effect",Json()); app.raw=app.draft.dump(2);
            app.dirtyDraft=app.rawDirty=false; ++app.editRevision; app.error.clear();
        } else app.applyThen({{"op", "undo"}});
    }
    ImGui::SameLine(); if (ImGui::Button("Redo")) app.applyThen({{"op", "redo"}});
    ImGui::SameLine(); if (ImGui::Button("Import JSON")) { app.filePath.clear(); ImGui::OpenPopup("Import JSON"); }
    ImGui::SameLine(); if (ImGui::Button("Export JSON")) { app.filePath = app.selected() + ".json"; ImGui::OpenPopup("Export JSON"); }
    ImGui::SameLine();
    const auto& toolbarStyle=ImGui::GetStyle();
    const float resourceTabsWidth=ImGui::CalcTextSize("Effects").x+ImGui::CalcTextSize("Textures").x+4*toolbarStyle.FramePadding.x+2*toolbarStyle.ItemInnerSpacing.x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX()+16*uiScale,ImGui::GetWindowWidth()-toolbarStyle.WindowPadding.x-resourceTabsWidth));
    // Include the gap above the divider in the tabs' backgrounds and hit areas.
    const ImVec2 tabPadding(toolbarStyle.FramePadding.x,toolbarStyle.FramePadding.y+toolbarStyle.ItemSpacing.y*.5f);
    const ImVec2 tabSpacing(toolbarStyle.ItemSpacing.x,0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,tabPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,tabSpacing);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize,0.f);
    if (ImGui::BeginTabBar("Resources")) {
        if (ImGui::BeginTabItem("Effects",nullptr,app.selectResourceTab==0 ? ImGuiTabItemFlags_SetSelected : 0)) { app.textureMode=false; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Textures",nullptr,app.selectResourceTab==1 ? ImGuiTabItemFlags_SetSelected : 0)) { app.textureMode=true; ImGui::EndTabItem(); }
        app.selectResourceTab=-1;
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar(3);
    ImGui::PushStyleColor(ImGuiCol_Separator,ImGui::GetStyle().Colors[ImGuiCol_TabSelected]);
    ImGui::Separator();
    ImGui::PopStyleColor();
    if (app.textureMode) {
        if (ImGui::Button("Add texture")) { app.resourceName="NewTexture"; app.filePath.clear(); ImGui::OpenPopup("Add texture"); }
        ImGui::BeginDisabled(app.selectedTexture.empty());
        ImGui::SameLine(); if (ImGui::Button("Rename texture")) { app.resourceName=app.selectedTexture; ImGui::OpenPopup("Rename texture"); }
        ImGui::SameLine(); if (ImGui::Button("Replace texture")) { app.filePath.clear(); ImGui::OpenPopup("Replace texture"); }
        ImGui::SameLine(); if (ImGui::Button("Delete texture")) { app.replacementTexture.clear(); ImGui::OpenPopup("Delete texture"); }
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Add effect")) { app.resourceName="NewEffect"; ImGui::OpenPopup("Add effect"); }
        ImGui::BeginDisabled(app.selected().empty());
        ImGui::SameLine(); if (ImGui::Button("Rename effect")) { app.resourceName=app.selected(); ImGui::OpenPopup("Rename effect"); }
        ImGui::SameLine(); if (ImGui::Button("Delete effect")) ImGui::OpenPopup("Delete effect");
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled(); ImGui::EndDisabled();
    dialogs(app);
    if (app.busy) { ImGui::SameLine(); ImGui::TextDisabled("Working…"); }
    if (!app.error.empty()) ImGui::TextWrapped("%s", app.error.c_str());
    ImGui::Separator();
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("effects", ImVec2(std::clamp(width * .19f, 180.f*uiScale, 300.f*uiScale), height), true);
    ImGui::TextUnformatted(app.textureMode ? "TEXTURES" : "EFFECTS"); ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##search", "Filter resources", &app.search);
    if (app.loaded()) {
        ImGui::BeginDisabled(app.busy || app.disconnected || app.dirtyDraft || app.rawDirty);
        const auto& entries=app.state[app.textureMode ? "textures" : "names"];
        for (const auto& entry : entries) {
            const auto name = app.textureMode ? entry.at("name").get<std::string>() : entry.get<std::string>();
            std::string lower = name, filter = app.search;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return std::tolower(c); });
            if (lower.find(filter) != std::string::npos && ImGui::Selectable(name.c_str(), name == (app.textureMode ? app.selectedTexture : app.selected()))) {
                if (app.textureMode) app.selectedTexture=name;
                else app.applyThen({{"op", "select"}, {"name", name}});
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("properties", ImVec2(width * .40f, height), true);
    ImGui::TextUnformatted(app.loaded() ? (app.textureMode ? app.selectedTexture : app.selected()).c_str() : "PROPERTIES");
    if (app.textureMode && app.loaded()) {
        if (const auto* item=app.texture(app.selectedTexture)) {
            textureImage(app,app.selectedTexture,std::min(300.f*uiScale,ImGui::GetContentRegionAvail().x));
            ImGui::Text("%d x %d | GX format %d",item->at("width").get<int>(),item->at("height").get<int>(),item->at("format").get<int>());
            ImGui::Separator(); ImGui::TextUnformatted("USED BY");
            if (item->at("dependencies").empty()) ImGui::TextDisabled("No effects reference this texture.");
            ImGui::BeginDisabled(app.busy || app.disconnected || app.dirtyDraft || app.rawDirty);
            for (const auto& dependency:item->at("dependencies")) {
                const auto name=dependency.at("effect").get<std::string>(),path=dependency.at("field").get<std::string>();
                ImGui::PushID(path.c_str());
                if (ImGui::Selectable(name.c_str())) { app.applyThen({{"op","select"},{"name",name}}); app.textureMode=false; app.selectResourceTab=0; }
                ImGui::TextWrapped("%s",path.c_str()); ImGui::PopID();
            }
            ImGui::EndDisabled();
        }
    } else if (!app.draft.is_null()) {
        ImGui::BeginDisabled(app.disconnected || (app.busy && app.activeOperation!="replace"));
        if (ImGui::BeginTabBar("editor tabs")) {
            if (ImGui::BeginTabItem("Fields")) {
                bool commit = false;
                ImGui::BeginDisabled(app.rawDirty);
                if(app.draft.is_object()) {
                    try {
                        if(app.formRevision!=app.editRevision) {app.form=Json(breff::codec::effectForm(breff::codec::Json(app.draft)));app.formRevision=app.editRevision;}
                        if(editForm(app,app.form,commit)){app.dirtyDraft=true;++app.editRevision;app.raw=app.draft.dump(2);}
                    } catch(const std::exception& e) {app.error=e.what();}
                }
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("JSON")) {
                app.editingRaw=true;
                if (ImGui::InputTextMultiline("##json", &app.raw, ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput)) {
                    app.rawDirty=true; ++app.editRevision;
                    try {
                        auto parsed = Json::parse(app.raw);
                        if (!parsed.is_object()) throw std::runtime_error("An effect must be a JSON object");
                        app.draft = std::move(parsed); app.dirtyDraft = true; app.rawDirty = false;
                    }
                    catch (const std::exception& e) { app.error = e.what(); }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndDisabled();
    } else if (app.loaded()) {
        auto errors = app.state.value("errors", Json::object());
        ImGui::TextWrapped("%s", errors.value(app.selected(), "No editable effect selected").c_str());
    }
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("preview", ImVec2(0, height), true, ImGuiWindowFlags_NoBackground);
    ImGui::TextUnformatted("PREVIEW");
    if(!app.state.value("missingTextures",Json::array()).empty()) {
        ImGui::SameLine();
        if(ImGui::SmallButton("Missing textures..."))app.requestMissingTextures=true;
    }
    ImGui::Checkbox("Loop",&app.loop); ImGui::SameLine();
    ImGui::Checkbox("Grid",&app.showGrid); ImGui::SameLine();
    ImGui::Checkbox("Axes",&app.showAxes);
    ImGui::Separator();
    if (ImGui::Checkbox("Use file's random seed", &app.preview.useResourceSeed)) app.preview.replay();
    ImGui::BeginDisabled(app.preview.useResourceSeed);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##preview-seed", &app.preview.seed, 0, 65535, "Seed: %d", ImGuiSliderFlags_AlwaysClamp)) app.preview.replay();
    if (ImGui::Button("Previous variation")) { app.preview.seed = (app.preview.seed + 65535) & 65535; app.preview.replay(); }
    ImGui::SameLine();
    if (ImGui::Button("Next variation")) { app.preview.seed = (app.preview.seed + 1) & 65535; app.preview.replay(); }
    ImGui::EndDisabled();
    if (ImGui::Button("Replay")) app.preview.replay();
    ImGui::SameLine();
    if (ImGui::Button("Reset camera (R)")) app.camera.reset();
    ImGui::TextDisabled("Preview seed overrides are not saved to the file.");
    ImGui::Separator();
    if (!app.previewError.empty()) ImGui::TextWrapped("%s",app.previewError.c_str());
    if (app.loaded()) {
        ImGui::Spacing();
    }
    ImGui::TextDisabled("Wheel: zoom  |  Left-drag: orbit  |  R: reset");
    const ImVec2 canvas = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##orbit-view", ImVec2(std::max(1.f,canvas.x),std::max(1.f,canvas.y)), ImGuiButtonFlags_MouseButtonLeft);
    auto origin=ImGui::GetItemRectMin(),end=ImGui::GetItemRectMax(),scale=ImGui::GetIO().DisplayFramebufferScale;
    app.previewRect={origin.x*scale.x,origin.y*scale.y,(end.x-origin.x)*scale.x,(end.y-origin.y)*scale.y};
    if (ImGui::IsItemHovered()) {
        app.camera.zoom(ImGui::GetIO().MouseWheel);
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_R)) app.camera.reset();
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const auto delta = ImGui::GetIO().MouseDelta;
        app.camera.rotate(delta.x,delta.y);
    }
    ImGui::EndChild();
    ImGui::End();
}

void logCallback(AuroraLogLevel, const char* module, const char* message, unsigned int size) {
    std::fprintf(stderr, "[%s] %.*s\n", module, static_cast<int>(size), message);
}
}

int main(int argc, char** argv) {
    bool smokeTest = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--smoke-test") smokeTest = true;
    int renderedFrames = 0;
    AuroraConfig config{};
    config.appName = "Effect Editor"; config.windowWidth = 1500; config.windowHeight = 950;
    config.windowPosX = config.windowPosY = SDL_WINDOWPOS_CENTERED;
    config.vsync = true; config.logCallback = logCallback; config.desiredBackend = BACKEND_AUTO;
    config.imGuiInitCallback = [](const AuroraWindowSize* size) { updateDpi(*size); };
    const auto info = aurora_initialize(argc, argv, &config);
    // Aurora initializes the GPU backend; GXInit separately initializes the
    // SDK's register addresses and default state used by every GX setter.
    GXInit(nullptr, 0);
    Application app; app.window = info.window;
    png_image logo{};
    logo.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_memory(&logo, editorLogo, sizeof(editorLogo))) {
        logo.format = PNG_FORMAT_RGBA;
        std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(logo));
        if (png_image_finish_read(&logo, nullptr, pixels.data(), 0, nullptr)) {
            if (auto* icon = SDL_CreateSurfaceFrom(logo.width, logo.height, SDL_PIXELFORMAT_RGBA32,
                                                   pixels.data(), logo.width * 4)) {
                SDL_SetWindowIcon(app.window, icon);
                SDL_DestroySurface(icon);
            }
        }
        png_image_free(&logo);
    }
    placeWindow(app.window);
    std::vector<std::string> paths;
    std::string initialEffect,initialTexture;
    for(int i=1;i<argc;++i) {
        const std::string argument=argv[i];
        if(argument=="--effect" && i+1<argc)initialEffect=argv[++i];
        else if(argument=="--texture" && i+1<argc)initialTexture=argv[++i];
        else if(!argument.starts_with("--"))paths.push_back(argument);
    }
    if(!paths.empty())app.enqueue({{"op","open"},{"breff",paths[0]},{"breft",paths.size()>1?Json(paths[1]):Json()}});
    else app.enqueue({{"op","state"}});
    if(!initialEffect.empty())app.enqueue({{"op","select"},{"name",initialEffect}});
    if(!initialTexture.empty()){app.selectedTexture=initialTexture;app.textureMode=true;app.selectResourceTab=1;}
    app.flush();
    while (!app.quitting) {
        bool close = false;
        for (const AuroraEvent* event = aurora_update(); event && event->type != AURORA_NONE; ++event) if (event->type == AURORA_EXIT) close = true;
        app.pollInput(); app.pollDialog();
        updateDpi(app.window);
        if (!aurora_begin_frame()) { SDL_Delay(10); continue; }
        GXSetCopyClear(GXColor{19, 22, 28, 255}, GX_MAX_Z24);
        drawUI(app, close);
        drawPreviewGrid(app.previewRect,app.camera,app.showGrid,app.showAxes);
        if (app.loaded() && !app.selected().empty() && app.loadedGeneration==app.state.value("generation",0)) {
            try {
                if (app.playingEffect!=app.selected() || app.playingGeneration!=app.loadedGeneration || app.playingRestart!=app.preview.restart) {
                    app.playingEffect=app.selected(); app.playingGeneration=app.loadedGeneration;
                    app.playingRestart=app.preview.restart; app.simulationTime=0; app.previewError.clear();
                    app.engine.start(app.archive,app.selected(),app.preview.useResourceSeed ? std::nullopt : std::optional<uint16_t>(app.preview.selectedSeed()));
                }
                if (app.previewError.empty()) {
                    app.simulationTime+=std::min(double(ImGui::GetIO().DeltaTime),.25);
                    while (app.simulationTime>=1.0/60) {
                        app.engine.step(); app.simulationTime-=1.0/60;
                        if (app.loop && app.engine.finished()) app.engine.start(app.archive,app.selected(),app.preview.useResourceSeed ? std::nullopt : std::optional<uint16_t>(app.preview.selectedSeed()));
                    }
                    nw4r::ef::DrawInfo drawInfo;
                    nw4r::math::MTX34 view; app.camera.view(view.m);
                    drawInfo.SetViewMtx(view);
                    app.engine.draw(drawInfo);
                }
            } catch (const std::exception& e) {
                app.previewError=e.what(); app.engine.reset();
                std::fprintf(stderr,"Preview failed: %s\n",e.what());
            }
        }
        aurora_end_frame();
        if (++renderedFrames == 120 && smokeTest) {
            std::fprintf(stderr, "BREFF editor rendered 120 frames; active particles: %u\n",app.engine.particles());
            app.quitting = true;
        }
        if (ImGui::GetTime()-app.lastSubmission>.12) app.apply();
        app.flush();
    }
    app.engine.reset();
    aurora_shutdown();
    app.images.clear();
    return smokeTest && (!app.previewError.empty() || !app.error.empty()) ? 1 : 0;
}
