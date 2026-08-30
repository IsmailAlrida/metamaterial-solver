#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

#include "icons.hpp"
#include "glvis_adapter.hpp"
#include "imgui_internal.h"
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

Renderer::Renderer(AppSettings& settings, SolverResult& result, LevelSet& geometry)
    : settings(settings),
      result(result),
      geometry(geometry)
{
}

Renderer::~Renderer()
{
    if (window != nullptr && glContext != nullptr) {
        SDL_GL_MakeCurrent(window, glContext);
    }

    glvis.reset();

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
    glvis = std::make_unique<GlvisAdapter>();
}

void Renderer::displayFrame(dispatcher_t& dispatcher)
{
    if (!openglBackendInitialized) {
        throw std::logic_error("Renderer::setup() must succeed before displayFrame()");
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        glvis->processEvent(event);
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

    StartMenu(dispatcher);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImHashStr("MetamaterialDockSpace");
    setupInitialDockLayout(dockspaceId, *viewport);
    ImGui::DockSpaceOverViewport(dockspaceId, viewport);

    SimulationSettingsPanel(dispatcher);
    SimulationInfoPanel();
    glvis->draw();
    OptimizerDesignPanel(dispatcher);
    ActionPanel(dispatcher);
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

void Renderer::setupInitialDockLayout(ImGuiID dockspaceId,
                                      const ImGuiViewport& viewport)
{
    // DockBuilder is an internal and deliberately isolated API. If ImGui loaded a
    // saved node from imgui.ini, leave the user's layout alone.
    if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr) {
        return;
    }

    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport.WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport.WorkSize);

    ImGuiID top = dockspaceId;
    ImGuiID bottom = 0;
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Down, 0.22f, &bottom, &top);

    ImGuiID left = 0;
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.24f, &left, &top);

    ImGuiID right = 0;
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Right, 0.24f, &right, &top);

    ImGuiID leftInfo = 0;
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.34f, &leftInfo, &left);

    ImGuiID frequency = 0;
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Down, 0.38f, &frequency, &top);

    ImGuiID runControl = 0;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.34f, &runControl, &right);

    ImGui::DockBuilderDockWindow("Simulation Settings", left);
    ImGui::DockBuilderDockWindow("Simulation Information", leftInfo);
    ImGui::DockBuilderDockWindow("Visualization", top);
    ImGui::DockBuilderDockWindow("Frequency Response", frequency);
    ImGui::DockBuilderDockWindow("Objective Design", right);
    ImGui::DockBuilderDockWindow("Run Control", runControl);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

void Renderer::StartMenu(const dispatcher_t& dispatcher)
{
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    // TODO: Increase size and bottom padding a little here

    ImGui::TextColored(ImVec4(0.39f, 0.70f, 1.0f, 1.0f), "METAMATERIAL LAB");
    ImGui::Separator();

    // if (ImGui::BeginMenu("File")) {
    //     if (ImGui::MenuItem("Exit", "Alt+F4")) {
    //         shouldClose = true;
    //     }
    //     ImGui::EndMenu();
    // }

    // if (ImGui::BeginMenu("View")) {
    //     if (ImGui::MenuItem("Simulation Settings")) {
    //         ImGui::SetWindowFocus("Simulation Settings");
    //     }
    //     if (ImGui::MenuItem("Visualization")) {
    //         ImGui::SetWindowFocus("Visualization");
    //     }
    //     if (ImGui::MenuItem("Frequency Response")) {
    //         ImGui::SetWindowFocus("Frequency Response");
    //     }
    //     if (ImGui::MenuItem("Console")) {
    //         ImGui::SetWindowFocus("Console");
    //     }
    //     ImGui::EndMenu();
    // }

    const char* state = "IDLE";
    ImVec4 stateColor(0.52f, 0.61f, 0.72f, 1.0f);
    if (dispatcher.get_state() == dispatcher_t::State::Working) {
        state = "RUNNING";
        stateColor = ImVec4(0.30f, 0.82f, 0.61f, 1.0f);
    }
    else if (dispatcher.get_state() == dispatcher_t::State::Exporting) {
        state = "EXPORTING";
        stateColor = ImVec4(0.78f, 0.61f, 0.28f, 1.0f);
    }
    else if (dispatcher.get_state() == dispatcher_t::State::Error
        || dispatcher.get_optimizer_status() == OptimizerStatus::Diverged
        || dispatcher.get_optimizer_status() == OptimizerStatus::Error) {
        state = "ERROR";
        stateColor = ImVec4(1.0f, 0.38f, 0.38f, 1.0f);
    }
    else if (dispatcher.get_optimizer_status() == OptimizerStatus::Converged
        || dispatcher.get_optimizer_status() == OptimizerStatus::MaximumIterations) {
        state = "COMPLETE";
        stateColor = ImVec4(0.35f, 0.69f, 1.0f, 1.0f);
    }
    else if (dispatcher.get_optimizer_status() == OptimizerStatus::Cancelled) {
        state = "CANCELLED";
        stateColor = ImVec4(0.95f, 0.72f, 0.28f, 1.0f);
    }

    const float statusWidth = 245.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - statusWidth));
    ImGui::SameLine();
    ImGui::TextColored(stateColor, "%s", state);
    ImGui::EndMainMenuBar();
}

void Renderer::SimulationSettingsPanel(const dispatcher_t& dispatcher)
{
    if (!ImGui::Begin("Simulation Settings")) {
        ImGui::End();
        return;
    }

    SolverSettings& solver = settings.solverSettings;
    OptimizerSettings& optimizer = settings.optSettings;
    const bool locked = dispatcher.get_state() != dispatcher_t::State::Idle;

    if (locked) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.72f, 0.28f, 1.0f),
            "Settings are locked during an operation.");
    }

    ImGui::BeginDisabled(locked);
    if (ImGui::CollapsingHeader("Domain & mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool is3D = solver.nz > 0;
        if (ImGui::Checkbox("Extrude to 3D", &is3D)) {
            solver.nz = is3D ? std::max(solver.ny, 1) : 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(is3D ? "3D hexahedral grid" : "nz = 0, 2D quadrilateral grid");

        ImGui::SeparatorText("Duct regions along x");
        const double fixedRegionMinimum = 0.02;
        const double fixedRegionMaximum = 0.30;
        const double designRegionMinimum = 0.05;
        const double designRegionMaximum = 1.00;
        ImGui::SliderScalar("Inlet (m)", ImGuiDataType_Double,
            &solver.inletLength, &fixedRegionMinimum,
            &fixedRegionMaximum, "%.4f");
        ImGui::SliderScalar("Design (m)", ImGuiDataType_Double,
            &solver.designLength, &designRegionMinimum,
            &designRegionMaximum, "%.4f");
        ImGui::SliderScalar("Outlet (m)", ImGuiDataType_Double,
            &solver.outletLength, &fixedRegionMinimum,
            &fixedRegionMaximum, "%.4f");

        const double sx = solver.inletLength
            + solver.designLength
            + solver.outletLength;
        ImGui::TextDisabled("Total x extent: %.4f m", sx);

        ImGui::SeparatorText("Mesh resolution");
        ImGui::Checkbox("Isotropic grid", &solver.isotropicGrid);
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputInt("nx", &solver.nx);

        if (!solver.isotropicGrid) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputInt("ny", &solver.ny);
            if (is3D) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110.0f);
                ImGui::InputInt("nz", &solver.nz);
            }

        }

        const double heightMinimum = 0.02;
        const double heightMaximum = 0.30;
        ImGui::SliderScalar("sy (m)", ImGuiDataType_Double,
            &solver.sy, &heightMinimum, &heightMaximum, "%.4f");
        if (is3D) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputDouble("sz (m)", &solver.sz, 0.01, 0.1, "%.4f");
        }

        solver.nx = std::max(solver.nx, 1);
        solver.sy = std::clamp(solver.sy, heightMinimum, heightMaximum);
        solver.sz = std::max(solver.sz, 1.0e-6);

        if (solver.isotropicGrid) {
            const double hx = sx / solver.nx;
            solver.ny = std::max(1, static_cast<int>(std::lround(solver.sy / hx)));
            solver.nz = is3D
                ? std::max(1, static_cast<int>(std::lround(solver.sz / hx)))
                : 0;
            if (is3D) {
                ImGui::TextDisabled("Derived grid: %d x %d x %d", solver.nx, solver.ny, solver.nz);
            }
            else {
                ImGui::TextDisabled("Derived grid: %d x %d", solver.nx, solver.ny);
            }
        }
        else {
            solver.ny = std::max(solver.ny, 1);
            solver.nz = is3D ? std::max(solver.nz, 1) : 0;
        }
    }

    if (ImGui::CollapsingHeader("Physics & materials", ImGuiTreeNodeFlags_DefaultOpen)) {
        int physicsIndex = std::holds_alternative<VibroacousticSettings>(solver.physics) ? 0 : 1;
        const char* physicsItems[] = {"Vibroacoustic", "Electromagnetic (WIP)"};
        if (ImGui::Combo("Physics problem", &physicsIndex, physicsItems, 2)) {
            solver.physics = physicsIndex == 0
                ? PhysicsSettings{VibroacousticSettings{}}
                : PhysicsSettings{ElectromagneticSettings{}};
        }

        if (VibroacousticSettings* physics =
                std::get_if<VibroacousticSettings>(&solver.physics)) {
            ImGui::InputFloat("Solid density rho_s (kg/m^3)", &physics->rho_s, 10.0f, 100.0f, "%.2f");
            ImGui::InputFloat("Fluid density rho_a (kg/m^3)", &physics->rho_a, 0.01f, 0.1f, "%.3f");
            ImGui::InputFloat("Speed of sound c_a (m/s)", &physics->c_a, 1.0f, 10.0f, "%.2f");
            ImGui::InputFloat("Young's modulus (Pa)", &physics->youngs_modulus, 1.0e6f, 1.0e7f, "%.3e");
            ImGui::SliderFloat("Poisson ratio", &physics->poisson_ratio, 0.0f, 0.49f, "%.3f");
            ImGui::InputFloat("Domain contrast epsilon", &physics->epsilon, 1.0e-9f, 1.0e-8f, "%.3e");

            ImGui::SeparatorText("Rayleigh damping calibration");
            ImGui::SliderFloat("Damping ratio", &physics->zeta, 0.0f, 0.3f, "%.4f");
            ImGui::InputFloat("Calibration frequency f1 (Hz)", &physics->f1, 10.0f, 100.0f, "%.1f");
            ImGui::InputFloat("Calibration frequency f2 (Hz)", &physics->f2, 10.0f, 100.0f, "%.1f");
        }
        else {
            ImGui::TextDisabled("Electromagnetic material settings are not implemented yet.");
        }
    }

    if (ImGui::CollapsingHeader("Fourier Transform & Solver Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputDouble("Duration (s)", &solver.duration, 0.001, 0.01, "%.5f");
        ImGui::InputDouble("Time step dt (s)", &solver.dt, 1.0e-6, 1.0e-5, "%.7f");
        ImGui::Checkbox("Hann window", &solver.useHannWindow);
        solver.duration = std::max(solver.duration, 1.0e-6);
        solver.dt = std::max(solver.dt, 1.0e-9);
        ImGui::TextDisabled("Derived FFT samples: %d",
            static_cast<int>(std::llround(solver.duration / solver.dt)));
        ImGui::InputDouble("Newmark beta", &solver.newmarkBeta, 0.01, 0.1, "%.4f");
        ImGui::InputDouble("Newmark gamma", &solver.newmarkGamma, 0.01, 0.1, "%.4f");
        ImGui::InputDouble("Source amplitude (Pa)", &solver.sourceAmplitude, 0.1, 1.0, "%.3f");
        ImGui::InputScalar("Source seed", ImGuiDataType_U32, &solver.sourceSeed);
    }

    if (ImGui::CollapsingHeader("Optimizer Settings")) {
        ImGui::InputFloat("Smoothing radius (m)", &optimizer.filterRadius, 0.0001f, 0.001f, "%.5f");
        ImGui::InputInt("Maximum iterations", &optimizer.maxIterations);
        ImGui::InputDouble("Initial asymptote", &optimizer.mmaInitialAsymptote, 0.01, 0.1, "%.3f");
        ImGui::InputDouble("Asymptote decrease", &optimizer.mmaDecreaseAsymptote, 0.01, 0.1, "%.3f");
        ImGui::InputDouble("Asymptote increase", &optimizer.mmaIncreaseAsymptote, 0.01, 0.1, "%.3f");
        ImGui::InputDouble("Constraint penalty", &optimizer.mmaConstraintPenalty, 10.0, 100.0, "%.1f");
        ImGui::InputDouble("Cut derivative / h", &optimizer.cutDerivativeRelativeStep, 1.0e-5, 1.0e-4, "%.2e");
        optimizer.filterRadius = std::max(optimizer.filterRadius, 0.0f);
        optimizer.maxIterations = std::clamp(optimizer.maxIterations, 1, 10000);
    }

    if (ImGui::CollapsingHeader("Initial level-set design")) {
        ImGui::InputInt("Cosine count x", &solver.initialPatternX);
        ImGui::InputInt("Cosine count y", &solver.initialPatternY);
        ImGui::InputDouble("Pattern length x (m)", &solver.initialPatternLx, 0.01, 0.1, "%.4f");
        ImGui::InputDouble("Pattern length y (m)", &solver.initialPatternLy, 0.01, 0.1, "%.4f");
        ImGui::InputDouble("Pattern bias", &solver.initialPatternBias, 0.01, 0.1, "%.4f");
        ImGui::InputDouble("Pattern threshold", &solver.initialPatternThreshold, 0.001, 0.01, "%.4f");
    }

    ImGui::EndDisabled();
    ImGui::End();
}

void Renderer::SimulationInfoPanel()
{
    if (!ImGui::Begin("Simulation Information")) {
        ImGui::End();
        return;
    }

    const SolverSettings& solver = settings.solverSettings;
    const bool is3D = solver.nz > 0;
    const long long elements = static_cast<long long>(solver.nx)
        * static_cast<long long>(solver.ny)
        * static_cast<long long>(is3D ? solver.nz : 1);
    const long long timeSteps = solver.dt > 0.0
        ? std::llround(solver.duration / solver.dt)
        : 0;
    const double sx = solver.inletLength
        + solver.designLength
        + solver.outletLength;

    ImGui::TextColored(ImVec4(0.35f, 0.69f, 1.0f, 1.0f), "%s ANALYSIS", is3D ? "3D" : "2D");
    ImGui::Text("%lld elements", elements);
    ImGui::Text("Duct: %.4f m = %.4f m inlet + %.4f m design + %.4f m outlet",
                sx,
                solver.inletLength,
                solver.designLength,
                solver.outletLength);
    ImGui::Text("%lld time steps", timeSteps);
    ImGui::Text("FFT: %lld samples%s", timeSteps, solver.useHannWindow ? " + Hann" : "");
    ImGui::Separator();
    ImGui::TextWrapped(
        "A Newmark time-domain solve produces the impulse response. The selected window is then applied before the FFT constructs the frequency response.");
    ImGui::Spacing();
    ImGui::TextDisabled("M u'' + C u' + K u = f(t)");
    ImGui::TextDisabled("H(f) = FFT(response) / FFT(excitation)");

    if (const VibroacousticSettings* physics =
            std::get_if<VibroacousticSettings>(&solver.physics)) {
        ImGui::SeparatorText("Material summary");
        ImGui::Text("rho_s  %.2f kg/m3", physics->rho_s);
        ImGui::Text("rho_a  %.3f kg/m3", physics->rho_a);
        ImGui::Text("c_a    %.2f m/s", physics->c_a);
        ImGui::Text("zeta   %.4f", physics->zeta);
    }
    ImGui::End();
}

void Renderer::updateObjectiveCurve()
{
    OptimizerSettings& optimizer = settings.optSettings;
    optimizer.frequencySamples = std::clamp(optimizer.frequencySamples, 32, 4096);
    optimizer.frequencyMax = std::max(optimizer.frequencyMax, optimizer.frequencyMin + 1.0f);
    optimizer.attenuationMaxDb = std::max(
        optimizer.attenuationMaxDb, optimizer.attenuationMinDb + 1.0f);

    const int count = optimizer.frequencySamples;
    objectiveFrequency.resize(count);
    objectiveTarget.assign(count, std::numeric_limits<float>::quiet_NaN());

    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        objectiveFrequency[i] = optimizer.frequencyMin
            + t * (optimizer.frequencyMax - optimizer.frequencyMin);

        const FrequencyBand* selected = nullptr;
        for (const FrequencyBand& band : optimizer.frequencyBands) {
            if (objectiveFrequency[i] >= band.startHz
                && objectiveFrequency[i] <= band.endHz
                && (selected == nullptr || band.endHz < selected->endHz)) {
                selected = &band;
            }
        }
        if (selected) {
            objectiveTarget[i] = static_cast<float>(std::clamp(
                20.0 * std::log10(std::max(
                    selected->targetTransmission, 1.0e-12)),
                static_cast<double>(optimizer.attenuationMinDb),
                static_cast<double>(optimizer.attenuationMaxDb)));
        }
    }
}

void Renderer::OptimizerDesignPanel(const dispatcher_t& dispatcher)
{
    OptimizerSettings& optimizer = settings.optSettings;
    const bool locked = dispatcher.get_state() != dispatcher_t::State::Idle;
    if (!locked) {
        updateObjectiveCurve();
    }

    if (ImGui::Begin("Frequency Response")) {
        ImGui::TextDisabled("Target objective and latest forward-solver response");
        const ImVec2 plotSize(-1.0f, -1.0f);
        if (ImPlot::BeginPlot("##frequency-response", plotSize)) {
            ImPlot::SetupAxes("Frequency (Hz)", "Attenuation (dB)");
            ImPlot::SetupAxesLimits(
                optimizer.frequencyMin,
                optimizer.frequencyMax,
                optimizer.attenuationMinDb,
                optimizer.attenuationMaxDb,
                ImPlotCond_Always);

            ImPlot::PlotShaded(
                "Target area",
                objectiveFrequency.data(),
                objectiveTarget.data(),
                static_cast<int>(objectiveTarget.size()),
                0.0f,
                {
                    ImPlotProp_FillColor, ImVec4(0.20f, 0.52f, 0.94f, 1.0f),
                    ImPlotProp_FillAlpha, 0.18f
                });

            ImPlot::PlotLine(
                "Target",
                objectiveFrequency.data(),
                objectiveTarget.data(),
                static_cast<int>(objectiveTarget.size()),
                {ImPlotProp_LineColor, ImVec4(0.35f, 0.69f, 1.0f, 1.0f)});

            const auto response = std::atomic_load(
                &result.materialImpulseResponse);
            const int responseCount = response
                ? std::min(
                    static_cast<int>(response->frequency.size()),
                    static_cast<int>(response->attenuationDB.size()))
                : 0;
            if (responseCount > 0) {
                ImPlot::PlotLine(
                    "Response",
                    response->frequency.data(),
                    response->attenuationDB.data(),
                    responseCount,
                    {ImPlotProp_LineColor, ImVec4(0.31f, 0.86f, 0.61f, 1.0f)});
            }
            ImPlot::EndPlot();
        }
    }
    ImGui::End();

    if (!ImGui::Begin("Objective Design")) {
        ImGui::End();
        return;
    }

    ImGui::BeginDisabled(locked);
    ImGui::SeparatorText("Plot range");
    ImGui::InputFloat("Minimum frequency (Hz)", &optimizer.frequencyMin, 10.0f, 100.0f, "%.1f");
    ImGui::InputFloat("Maximum frequency (Hz)", &optimizer.frequencyMax, 10.0f, 100.0f, "%.1f");
    ImGui::InputFloat("Minimum attenuation (dB)", &optimizer.attenuationMinDb, 1.0f, 5.0f, "%.1f");
    ImGui::InputFloat("Maximum attenuation (dB)", &optimizer.attenuationMaxDb, 1.0f, 5.0f, "%.1f");
    ImGui::InputInt("Frequency samples", &optimizer.frequencySamples, 16, 64);

    ImGui::SeparatorText("Frequency bands");
    if (ImGui::Button("+ Add band")) {
        double start = optimizer.frequencyMin;
        for (const FrequencyBand& band : optimizer.frequencyBands) {
            start = std::max(start, band.endHz);
        }
        const double width = std::max(
            1.0, 0.1 * (optimizer.frequencyMax - optimizer.frequencyMin));
        if (start < optimizer.frequencyMax) {
            optimizer.frequencyBands.push_back({
                FrequencyBandType::stop,
                start,
                std::min(
                    start + width,
                    static_cast<double>(optimizer.frequencyMax)),
                1.0e-2
            });
        }
        else {
            log(LogLevel::Warning,
                "Remove or shorten a frequency band before adding another.");
        }
    }

    int removeIndex = -1;
    if (ImGui::BeginChild(
            "##frequency-band-strip",
            ImVec2(0.0f, 185.0f),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_HorizontalScrollbar)) {
        for (std::size_t i = 0; i < optimizer.frequencyBands.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::BeginChild(
                    "##frequency-band-card",
                    ImVec2(235.0f, 155.0f),
                    ImGuiChildFlags_Borders)) {
                ImGui::TextColored(
                    ImVec4(0.35f, 0.69f, 1.0f, 1.0f),
                    "BAND %d",
                    static_cast<int>(i + 1));
                frequencyBandGroup(&optimizer.frequencyBands[i]);
                if (ImGui::SmallButton("Remove")) {
                    removeIndex = static_cast<int>(i);
                }
            }
            ImGui::EndChild();
            ImGui::PopID();
            if (i + 1 < optimizer.frequencyBands.size()) {
                ImGui::SameLine();
            }
        }
    }
    ImGui::EndChild();

    if (removeIndex >= 0) {
        optimizer.frequencyBands.erase(
            optimizer.frequencyBands.begin() + removeIndex);
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void Renderer::frequencyBandGroup(FrequencyBand* band)
{
    if (band == nullptr) {
        return;
    }

    int type = band->type == FrequencyBandType::pass ? 0 : 1;
    const char* types[] = {"Pass", "Stop"};
    if (ImGui::Combo("Type", &type, types, 2)) {
        band->type = type == 0
            ? FrequencyBandType::pass
            : FrequencyBandType::stop;
    }
    ImGui::InputDouble("Start (Hz)", &band->startHz, 10.0, 100.0, "%.1f");
    ImGui::InputDouble("End (Hz)", &band->endHz, 10.0, 100.0, "%.1f");
    double target_db = 20.0 * std::log10(std::max(
        band->targetTransmission, 1.0e-12));
    ImGui::InputDouble("Target (dB)", &target_db, 1.0, 5.0, "%.1f");
    band->startHz = std::clamp(
        band->startHz,
        static_cast<double>(settings.optSettings.frequencyMin),
        static_cast<double>(settings.optSettings.frequencyMax) - 1.0);
    band->endHz = std::clamp(
        band->endHz,
        band->startHz + 1.0,
        static_cast<double>(settings.optSettings.frequencyMax));
    target_db = std::clamp(
        target_db,
        static_cast<double>(settings.optSettings.attenuationMinDb),
        static_cast<double>(settings.optSettings.attenuationMaxDb));
    band->targetTransmission = std::pow(10.0, target_db / 20.0);
}

void Renderer::ActionPanel(dispatcher_t& dispatcher)
{
    if (!ImGui::Begin("Run Control")) {
        ImGui::End();
        return;
    }

    const char* state = "Ready";
    ImVec4 stateColor(0.52f, 0.61f, 0.72f, 1.0f);
    if (dispatcher.get_state() == dispatcher_t::State::Working) {
        state = "Running";
        stateColor = ImVec4(0.30f, 0.82f, 0.61f, 1.0f);
    }
    else if (dispatcher.get_state() == dispatcher_t::State::Exporting) {
        state = "Exporting";
        stateColor = ImVec4(0.78f, 0.61f, 0.28f, 1.0f);
    }
    else if (dispatcher.get_optimizer_status() == OptimizerStatus::Converged
        || dispatcher.get_optimizer_status() == OptimizerStatus::MaximumIterations) {
        state = dispatcher.get_optimizer_status()
                == OptimizerStatus::MaximumIterations
            ? "Maximum iterations reached!"
            : "Optimization complete";
        stateColor = ImVec4(0.35f, 0.69f, 1.0f, 1.0f);
    }
    else if (dispatcher.get_optimizer_status() == OptimizerStatus::Cancelled) {
        state = "Cancelled";
        stateColor = ImVec4(0.95f, 0.72f, 0.28f, 1.0f);
    }
    else if (dispatcher.get_state() == dispatcher_t::State::Error
        || dispatcher.get_optimizer_status() == OptimizerStatus::Diverged
        || dispatcher.get_optimizer_status() == OptimizerStatus::Error) {
        state = "Operation failed";
        stateColor = ImVec4(1.0f, 0.38f, 0.38f, 1.0f);
    }

    ImGui::TextDisabled("STATUS");
    ImGui::TextColored(stateColor, "%s", state);
    ImGui::Spacing();

    const int completedIterations = dispatcher.get_iteration();
    const int maximumIterations = std::max(settings.optSettings.maxIterations, 1);
    const float progress = std::clamp(
        static_cast<float>(completedIterations) / static_cast<float>(maximumIterations),
        0.0f,
        1.0f);
    const std::string progressLabel = std::to_string(completedIterations)
        + " / " + std::to_string(maximumIterations) + " iterations";
    ImGui::ProgressBar(progress, ImVec2(-1.0f, 2.0f), progressLabel.c_str());
    ImGui::Text("Phi pass  %.6g", dispatcher.get_pass_objective());
    ImGui::Text("Phi stop  %.6g", dispatcher.get_stop_objective());
    ImGui::Text("MMA z     %.6g", dispatcher.get_mma_bound());
    ImGui::Spacing();

    const bool canRun = dispatcher.get_state() == dispatcher_t::State::Idle;
    ImGui::BeginDisabled(!canRun);
#if METAMATERIAL_DEMO_MODE
    const char* runLabel = "Run demo optimization";
#else
    const char* runLabel = "Run optimization";
#endif
    if (ImGui::Button(runLabel, ImVec2(-1.0f, 38.0f))) {
        dispatcher.dispatch(dispatcher_t::Event::Start);
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled();
    ImGui::Button("Pause", ImVec2(-1.0f, 0.0f));
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Pause is not implemented yet.");
    }

    const bool canCancel =
        dispatcher.get_state() == dispatcher_t::State::Working;
    ImGui::BeginDisabled(!canCancel);
    if (ImGui::Button("Cancel run", ImVec2(-1.0f, 0.0f))) {
        ImGui::OpenPopup("Cancel optimization?");
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal("Cancel optimization?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Request cancellation after the current complete solve or iteration?");
        ImGui::Spacing();
        if (ImGui::Button("Cancel run", ImVec2(120.0f, 0.0f))) {
            dispatcher.dispatch(dispatcher_t::Event::Cancel);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep running", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const bool canExport = dispatcher.get_state() == dispatcher_t::State::Idle
        && dispatcher.is_exportable();
    ImGui::BeginDisabled(!canExport);
    if (ImGui::Button("Export mesh + manifest", ImVec2(-1.0f, 0.0f))) {
        dispatcher.dispatch(dispatcher_t::Event::Export);
    }
    ImGui::EndDisabled();

    ImGui::Separator();
#if METAMATERIAL_DEMO_MODE
    ImGui::TextColored(ImVec4(0.35f, 0.69f, 1.0f, 1.0f), "DEMO SOURCE");
    ImGui::TextDisabled("Deterministic data / single background workflow");
#else
    ImGui::TextColored(ImVec4(0.31f, 0.86f, 0.61f, 1.0f), "REAL OPTIMIZER");
    ImGui::TextDisabled("Serial MFEM / ParOpt MMA");
#endif
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
    if (!Icons::load(io, uiFont, 18.0f)) {
        log(LogLevel::Warning, "Material Symbols icon font could not be loaded.");
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
