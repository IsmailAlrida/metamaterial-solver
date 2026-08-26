#include "renderer.hpp"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <SDL.h>
#include <SDL_opengl.h>

namespace App {

Renderer::Renderer(AppSettings& settings)
    : settings(settings)
{
}

Renderer::~Renderer()
{
    if (window != nullptr && glContext != nullptr) {
        SDL_GL_MakeCurrent(window, glContext);
    }

    if (openglBackendInitialized) {
        ImGui_ImplOpenGL3_Shutdown();
    }
    if (sdlBackendInitialized) {
        ImGui_ImplSDL2_Shutdown();
    }
    if (implotContextCreated) {
        ImPlot::DestroyContext();
    }
    if (imguiContextCreated) {
        ImGui::DestroyContext();
    }

    if (glContext != nullptr) {
        SDL_GL_DeleteContext(glContext);
    }
    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
    if (sdlInitialized) {
        SDL_Quit();
    }
}

void Renderer::setup()
{
    if (sdlInitialized) {
        throw std::logic_error("Renderer::setup() may only be called once");
    }

#ifdef _WIN32
    ::SetProcessDPIAware();
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        throw std::runtime_error(std::string("SDL_Init(): ") + SDL_GetError());
    }
    sdlInitialized = true;

#if defined(IMGUI_IMPL_OPENGL_ES2)
    glslVersion = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    glslVersion = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    glslVersion = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    glslVersion = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const float mainScale = std::max(ImGui_ImplSDL2_GetContentScaleForDisplay(0), 1.0f);
    const auto windowFlags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    window = SDL_CreateWindow(
        "Metamaterial Solver",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        static_cast<int>(1440 * mainScale),
        static_cast<int>(900 * mainScale),
        windowFlags);
    if (window == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateWindow(): ") + SDL_GetError());
    }

    glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
        throw std::runtime_error(std::string("SDL_GL_CreateContext(): ") + SDL_GetError());
    }
    if (SDL_GL_MakeCurrent(window, glContext) != 0) {
        throw std::runtime_error(std::string("SDL_GL_MakeCurrent(): ") + SDL_GetError());
    }
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextCreated = true;
    ImPlot::CreateContext();
    implotContextCreated = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    applyGlobalStyle(mainScale);
    loadFonts();

    if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext)) {
        throw std::runtime_error("ImGui_ImplSDL2_InitForOpenGL() failed");
    }
    sdlBackendInitialized = true;

    if (!ImGui_ImplOpenGL3_Init(glslVersion)) {
        throw std::runtime_error("ImGui_ImplOpenGL3_Init() failed");
    }
    openglBackendInitialized = true;
}

void Renderer::displayFrame()
{
    if (!openglBackendInitialized) {
        throw std::logic_error("Renderer::setup() must succeed before displayFrame()");
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) {
            shouldClose = true;
        }
        if (event.type == SDL_WINDOWEVENT
            && event.window.event == SDL_WINDOWEVENT_CLOSE
            && event.window.windowID == SDL_GetWindowID(window)) {
            shouldClose = true;
        }
    }

    if (shouldClose) {
        return;
    }
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
        SDL_Delay(10);
        return;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport();
    GlvisPanel();
    LogPanel();

    ImGui::Render();
    const ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
    glClearColor(
        clearColor.x * clearColor.w,
        clearColor.y * clearColor.w,
        clearColor.z * clearColor.w,
        clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
        SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backupWindow, backupContext);
    }

    SDL_GL_SwapWindow(window);
}

void Renderer::GlvisPanel()
{
    if (!ImGui::Begin("Visualization")) {
        ImGui::End();
        return;
    }

    // TODO: Embed GLVis here while preserving its existing network stream API.
    ImGui::TextDisabled("GLVis visualization is not connected yet.");
    ImGui::End();
}

void Renderer::applyGlobalStyle(float scale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 5.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.90f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.45f, 0.51f, 0.60f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.035f, 0.047f, 0.067f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.045f, 0.059f, 0.082f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.055f, 0.071f, 0.098f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.15f, 0.19f, 0.25f, 0.85f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.075f, 0.094f, 0.125f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.11f, 0.16f, 0.23f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.13f, 0.20f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.035f, 0.047f, 0.067f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.055f, 0.075f, 0.105f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.035f, 0.047f, 0.067f, 0.85f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.045f, 0.059f, 0.082f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.035f, 0.047f, 0.067f, 0.75f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.18f, 0.23f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.25f, 0.32f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.30f, 0.39f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.30f, 0.58f, 0.96f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.25f, 0.51f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.36f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.14f, 0.31f, 0.55f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.20f, 0.43f, 0.76f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.16f, 0.36f, 0.65f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.12f, 0.25f, 0.43f, 0.75f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.18f, 0.39f, 0.68f, 0.85f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.16f, 0.34f, 0.60f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.15f, 0.19f, 0.25f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.28f, 0.55f, 0.93f, 1.00f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.25f, 0.51f, 0.90f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.30f, 0.58f, 0.96f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.36f, 0.65f, 1.00f, 0.95f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.20f, 0.43f, 0.76f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.075f, 0.12f, 0.19f, 1.00f);
    colors[ImGuiCol_TabSelected]           = ImVec4(0.14f, 0.31f, 0.55f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline]   = ImVec4(0.36f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_TabDimmed]             = ImVec4(0.045f, 0.059f, 0.082f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]     = ImVec4(0.09f, 0.17f, 0.28f, 1.00f);
    colors[ImGuiCol_DockingPreview]        = ImVec4(0.30f, 0.58f, 0.96f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.025f, 0.034f, 0.049f, 1.00f);
    colors[ImGuiCol_PlotLines]             = ImVec4(0.36f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.45f, 0.85f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = ImVec4(0.25f, 0.78f, 0.62f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.37f, 0.91f, 0.72f, 1.00f);
    colors[ImGuiCol_TextLink]              = ImVec4(0.38f, 0.67f, 1.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.20f, 0.43f, 0.76f, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = ImVec4(0.95f, 0.70f, 0.25f, 0.95f);
    colors[ImGuiCol_NavCursor]             = ImVec4(0.36f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);

    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        style.WindowRounding = 0.0f;
        colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImPlot::StyleColorsDark();
    ImPlotStyle& plotStyle = ImPlot::GetStyle();
    plotStyle.Colors[ImPlotCol_FrameBg] = colors[ImGuiCol_FrameBg];
    plotStyle.Colors[ImPlotCol_PlotBg] = ImVec4(0.025f, 0.034f, 0.049f, 1.00f);
    plotStyle.Colors[ImPlotCol_PlotBorder] = colors[ImGuiCol_Border];
    plotStyle.Colors[ImPlotCol_LegendBg] = colors[ImGuiCol_PopupBg];
    plotStyle.Colors[ImPlotCol_AxisText] = colors[ImGuiCol_TextDisabled];
    plotStyle.Colors[ImPlotCol_AxisGrid] = ImVec4(0.25f, 0.31f, 0.40f, 0.35f);
    plotStyle.Colors[ImPlotCol_Selection] = ImVec4(0.30f, 0.58f, 0.96f, 1.00f);
    plotStyle.PlotPadding = ImVec2(12.0f, 12.0f);
}

void Renderer::loadFonts()
{
    ImGuiIO& io = ImGui::GetIO();

#ifdef _WIN32
    char windowsDirectory[MAX_PATH]{};
    if (GetWindowsDirectoryA(windowsDirectory, MAX_PATH) != 0) {
        const std::filesystem::path fontsDirectory =
            std::filesystem::path(windowsDirectory) / "Fonts";

        const auto loadFirstAvailable = [&](std::initializer_list<const char*> names,
                                            float size) -> ImFont* {
            for (const char* name : names) {
                const std::filesystem::path path = fontsDirectory / name;
                if (std::filesystem::exists(path)) {
                    if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.string().c_str(), size)) {
                        return font;
                    }
                }
            }
            return nullptr;
        };

        uiFont = loadFirstAvailable({"segoeui.ttf", "SegoeUI.ttf"}, 17.0f);
        consoleFont = loadFirstAvailable(
            {"CascadiaMono.ttf", "CascadiaCode.ttf", "consola.ttf"}, 15.0f);
    }
#endif

    if (uiFont == nullptr) {
        uiFont = io.Fonts->AddFontDefault();
    }
    if (consoleFont == nullptr) {
        consoleFont = uiFont;
    }
    io.FontDefault = uiFont;
}

void Renderer::log(LogLevel level, std::string msg)
{
    // I mean cool mutex, tho i want a ring buffer, lock-free and threadsafe
    std::lock_guard<std::mutex> lock(logMutex);
    logEntries.push_back({level, std::move(msg)});
    logScrollToBottom = true;
}

void Renderer::LogPanel()
{
    if (!ImGui::Begin("Console")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(logMutex);
        logEntries.clear();
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255));

    if (ImGui::BeginChild("##console-output", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PushFont(consoleFont);
        bool scrollToBottom = false;
        {
            std::lock_guard<std::mutex> lock(logMutex);
            for (const LogEntry& entry : logEntries) {
                const char* label = "MESSAGE";
                ImVec4 color(0.82f, 0.86f, 0.92f, 1.0f);

                if (entry.level == LogLevel::Warning) {
                    label = "WARNING";
                    color = ImVec4(0.95f, 0.72f, 0.28f, 1.0f);
                }
                else if (entry.level == LogLevel::Error) {
                    label = "ERROR";
                    color = ImVec4(1.0f, 0.38f, 0.38f, 1.0f);
                }

                ImGui::TextColored(color, "[%s] %s", label, entry.message.c_str());
            }
            scrollToBottom = logScrollToBottom;
            logScrollToBottom = false;
        }

        if (scrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::PopFont();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace App
