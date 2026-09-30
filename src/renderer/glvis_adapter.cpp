#include "glvis_adapter.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <GL/glew.h>
#include <SDL2/SDL.h>

#include "imgui.h"
#include "aux_vis.hpp"
#include "glwindow.hpp"
#include "stream_reader.hpp"
#include "vsdata.hpp"
#include "vssolution.hpp"
#include "window.hpp"

thread_local mfem::GeometryRefiner GLVisGeometryRefiner;

namespace {

class ImGuiGlvisWindow final : public GLWindow {
    public:
        bool createWindow(const char*, int, int, int w, int h, bool legacyGlOnly) override
        {
            width = w;
            height = h;
            glewExperimental = GL_TRUE;
            initialized = initGLEW(legacyGlOnly);
            return initialized;
        }

        void mainLoop() override {}
        void mainIter() override {}
        void signalLoop() override {}
        void signalQuit() override { initialized = false; }
        void signalSwap() override { wnd_state = RenderState::Updated; }
        void signalKeyDown(SDL_Keycode key, SDL_Keymod modifiers) override
        {
            dispatchKey(key, modifiers);
        }

        void getWindowSize(int& w, int& h) const override
        {
            w = width;
            h = height;
        }

        void getGLDrawSize(int& w, int& h) const override
        {
            w = width;
            h = height;
        }

        void getDpi(int& w, int& h) const override
        {
            float diagonal = 96.0f;
            float horizontal = 96.0f;
            float vertical = 96.0f;
            SDL_GetDisplayDPI(0, &diagonal, &horizontal, &vertical);
            w = static_cast<int>(horizontal);
            h = static_cast<int>(vertical);
        }

        bool isHighDpi() const override
        {
            int horizontal = 0;
            int vertical = 0;
            getDpi(horizontal, vertical);
            return std::max(horizontal, vertical) >= 144;
        }

        void setWindowSize(int w, int h) override
        {
            width = w;
            height = h;
        }

        bool isWindowInitialized() const override { return initialized; }
        bool isGlInitialized() const override { return initialized; }

        void tick()
        {
            if (onIdle) {
                onIdle();
            }
        }

        void keyDown(const SDL_Keysym& key)
        {
            const char* scanName = SDL_GetScancodeName(key.scancode);
            if (scanName[0] >= 32 && scanName[0] < 127
                && scanName[1] == '\0'
                && (key.mod & (KMOD_CTRL | KMOD_LALT | KMOD_GUI)) == 0) {
                pendingText = true;
                pendingKey = key.sym;
                pendingModifiers = static_cast<SDL_Keymod>(key.mod);
                return;
            }

            pendingText = false;
            dispatchKey(key.sym, static_cast<SDL_Keymod>(key.mod));
        }

        void textInput(const SDL_TextInputEvent& event)
        {
            if (!pendingText) {
                return;
            }

            SDL_Keycode key = static_cast<unsigned char>(event.text[0]);
            if (onKeyDown.find(key) == onKeyDown.end()) {
                key = pendingKey;
            }
            dispatchKey(
                key,
                static_cast<SDL_Keymod>(
                    pendingModifiers & ~(KMOD_CAPS | KMOD_LSHIFT | KMOD_RSHIFT)));
            pendingText = false;
        }

        void mouseDown(Uint8 button, int x, int y)
        {
            MouseEventInfo event{x, y, SDL_GetModState()};
            if (onMouseDown[button]) {
                onMouseDown[button](&event);
            }
        }

        void mouseUp(Uint8 button, int x, int y)
        {
            MouseEventInfo event{x, y, SDL_GetModState()};
            if (onMouseUp[button]) {
                onMouseUp[button](&event);
            }
        }

        void mouseMove(Uint8 button, int x, int y)
        {
            MouseEventInfo event{x, y, SDL_GetModState()};
            if (onMouseMove[button]) {
                onMouseMove[button](&event);
            }
        }

    private:
        void dispatchKey(SDL_Keycode key, SDL_Keymod modifiers)
        {
            const auto handler = onKeyDown.find(key);
            if (handler == onKeyDown.end()) {
                return;
            }
            handler->second(modifiers);
            recordKey(key, modifiers);
        }

        int width = 1;
        int height = 1;
        bool initialized = false;
        bool pendingText = false;
        SDL_Keycode pendingKey = SDLK_UNKNOWN;
        SDL_Keymod pendingModifiers = KMOD_NONE;
};

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle InvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle InvalidSocket = -1;
#endif

void closeSocket(SocketHandle socket)
{
    if (socket == InvalidSocket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

class LoopbackServer {
    public:
        explicit LoopbackServer(int port)
        {
            SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socket == InvalidSocket) {
                return;
            }

            int reuseAddress = 1;
#ifdef _WIN32
            const char* option = reinterpret_cast<const char*>(&reuseAddress);
#else
            const void* option = &reuseAddress;
#endif
            if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                           option, sizeof(reuseAddress)) != 0) {
                closeSocket(socket);
                return;
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<unsigned short>(port));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::bind(socket, reinterpret_cast<const sockaddr*>(&address),
                       sizeof(address)) != 0
                || ::listen(socket, 1) != 0) {
                closeSocket(socket);
                return;
            }

            listeningSocket = socket;
        }

        ~LoopbackServer()
        {
            close();
        }

        bool good() const
        {
            return listeningSocket.load() != InvalidSocket;
        }

        int accept()
        {
            const SocketHandle socket = listeningSocket.load();
            if (socket == InvalidSocket) {
                return -1;
            }
            const SocketHandle client = ::accept(socket, nullptr, nullptr);
            return client == InvalidSocket ? -1 : static_cast<int>(client);
        }

        void close()
        {
            closeSocket(listeningSocket.exchange(InvalidSocket));
        }

    private:
        std::atomic<SocketHandle> listeningSocket{InvalidSocket};
};

std::filesystem::path findGlvisFont()
{
#ifdef _WIN32
    const std::filesystem::path candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf"
    };
#elif defined(__APPLE__)
    const std::filesystem::path candidates[] = {
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf"
    };
#else
    const std::filesystem::path candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"
    };
#endif

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace

namespace App {

struct GlvisAdapter::Impl {
    Impl()
        : server(GlvisAdapter::Port)
    {
        if (!server.good()) {
            setStatus("Could not listen on port " + std::to_string(GlvisAdapter::Port) + ".");
            return;
        }

        setStatus("Waiting for a local GLVis stream on 127.0.0.1:"
                  + std::to_string(GlvisAdapter::Port) + "...");
        listener = std::thread([this]() { listen(); });
    }

    ~Impl()
    {
        stopping = true;
        server.close();
        {
            std::lock_guard<std::mutex> lock(socketMutex);
            if (activeSocket != nullptr) {
                activeSocket->close();
            }
        }
        if (listener.joinable()) {
            listener.join();
        }

        destroySession();
        if (depthBuffer != 0) {
            glDeleteRenderbuffers(1, &depthBuffer);
        }
        if (colorTexture != 0) {
            glDeleteTextures(1, &colorTexture);
        }
        if (framebuffer != 0) {
            glDeleteFramebuffers(1, &framebuffer);
        }
    }

    void listen()
    {
        while (!stopping) {
            const int socketDescriptor = server.accept();
            if (socketDescriptor < 0) {
                if (!stopping) {
                    setStatus("GLVis listener stopped accepting connections.");
                }
                return;
            }
            auto socket = std::make_unique<mfem::socketstream>(socketDescriptor, false);

            {
                std::lock_guard<std::mutex> lock(socketMutex);
                activeSocket = socket.get();
            }
            setStatus("GLVis stream connected.");

            while (!stopping && socket->good()) {
                std::string dataType;
                *socket >> dataType >> std::ws;
                if (!socket->good()) {
                    break;
                }
                if (!StreamReader::SupportsDataType(dataType)) {
                    setStatus("Unsupported GLVis stream command: " + dataType);
                    break;
                }

                DataState nextState;
                StreamReader reader(nextState);
                if (reader.ReadStream(*socket, dataType) != 0) {
                    setStatus("GLVis could not parse the incoming " + dataType + " stream.");
                    break;
                }

                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    pendingState = std::move(nextState);
                }
                setStatus("Received GLVis " + dataType + " data.");
            }

            socket->close();
            {
                std::lock_guard<std::mutex> lock(socketMutex);
                activeSocket = nullptr;
            }
            if (!stopping) {
                setStatus("GLVis stream closed; waiting for another connection.");
            }
        }
    }

    void updateSession(int width, int height)
    {
        std::optional<DataState> nextState;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (pendingState) {
                nextState = std::move(pendingState);
                pendingState.reset();
            }
        }
        if (!nextState) {
            return;
        }

        const bool compatible = session
            && embeddedWindow
            && embeddedWindow->isWindowInitialized()
            && session->data_state.mesh
            && nextState->mesh
            && session->data_state.mesh->SpaceDimension() == nextState->mesh->SpaceDimension()
            && session->data_state.GetType() == nextState->GetType()
            && (((session->data_state.grid_f && nextState->grid_f)
                 && session->data_state.grid_f->VectorDim()
                    == nextState->grid_f->VectorDim())
                || (!session->data_state.grid_f && !nextState->grid_f));

        if (compatible) {
            if (!session->SetNewMeshAndSolution(std::move(*nextState))) {
                setStatus("GLVis rejected a supposedly compatible stream update.");
            }
            else {
                configureGeometry();
            }
            return;
        }

        destroySession();
        const std::filesystem::path font = findGlvisFont();
        if (font.empty()) {
            setStatus("GLVis could not find a system font.");
            return;
        }

        SetFont(font.string());
        session = std::make_unique<Window>();
        session->window_w = width;
        session->window_h = height;
        session->data_state = std::move(*nextState);
        auto hostWindow = std::make_unique<ImGuiGlvisWindow>();
        embeddedWindow = hostWindow.get();
        GLfloat previousClearColor[4]{};
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
        if (!session->GLVisInitVis({}, std::move(hostWindow))) {
            glClearColor(
                previousClearColor[0], previousClearColor[1],
                previousClearColor[2], previousClearColor[3]);
            embeddedWindow = nullptr;
            session.reset();
            setStatus("GLVis could not initialize the embedded visualization.");
            return;
        }
        glGetFloatv(GL_COLOR_CLEAR_VALUE, glvisClearColor);
        glClearColor(
            previousClearColor[0], previousClearColor[1],
            previousClearColor[2], previousClearColor[3]);

        configureGeometry();
    }

    bool render(int width, int height)
    {
        if (!session || !embeddedWindow || !embeddedWindow->isWindowInitialized()
            || width <= 0 || height <= 0) {
            return false;
        }
        if (!resizeFramebuffer(width, height)) {
            return false;
        }

        embeddedWindow->setWindowSize(width, height);
        embeddedWindow->tick();
        GLint previousDrawFramebuffer = 0;
        GLint previousReadFramebuffer = 0;
        GLint previousViewport[4]{};
        GLfloat previousClearColor[4]{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, width, height);
        glClearColor(
            glvisClearColor[0], glvisClearColor[1],
            glvisClearColor[2], glvisClearColor[3]);
        MyExpose();
        glGetFloatv(GL_COLOR_CLEAR_VALUE, glvisClearColor);

        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            static_cast<GLuint>(previousDrawFramebuffer));
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            static_cast<GLuint>(previousReadFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        glClearColor(
            previousClearColor[0], previousClearColor[1],
            previousClearColor[2], previousClearColor[3]);
        return true;
    }

    bool configureGeometry()
    {
        if (!session || !session->vs || !session->data_state.mesh
            || !session->data_state.grid_f) {
            return false;
        }

        if (session->data_state.mesh->SpaceDimension() == 2) {
            if (auto* scene = dynamic_cast<VisualizationSceneSolution*>(
                    session->vs.get())) {
                scene->SetDrawMesh(2);
                scene->SetLevelLines(0.0, 0.0, 1, 0);
                scene->UpdateLevelLines();
            }
        }
        setStatus("Geometry: solid phi > 0, air phi < 0, interface phi = 0.");
        return true;
    }

    int dimension() const
    {
        return session && session->data_state.mesh
            ? session->data_state.mesh->SpaceDimension()
            : 0;
    }

    void fitView()
    {
        if (!embeddedWindow) {
            return;
        }
        if (dimension() == 2) {
            embeddedWindow->callKeyDown('R');
        }
        else {
            embeddedWindow->callKeyDown('r');
        }
    }

    void processEvent(const SDL_Event& event)
    {
        if (!inputFocused || !embeddedWindow) {
            return;
        }
        GLfloat previousClearColor[4]{};
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
        glClearColor(
            glvisClearColor[0], glvisClearColor[1],
            glvisClearColor[2], glvisClearColor[3]);
        if (event.type == SDL_KEYDOWN) {
            embeddedWindow->keyDown(event.key.keysym);
        }
        else if (event.type == SDL_TEXTINPUT) {
            embeddedWindow->textInput(event.text);
        }
        glGetFloatv(GL_COLOR_CLEAR_VALUE, glvisClearColor);
        glClearColor(
            previousClearColor[0], previousClearColor[1],
            previousClearColor[2], previousClearColor[3]);
    }

    void setInputFocused(bool focused)
    {
        inputFocused = focused;
    }

    void mouseInput(const ImVec2& origin, bool hovered)
    {
        if (!embeddedWindow) {
            return;
        }

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int x = static_cast<int>(mouse.x - origin.x);
        const int y = static_cast<int>(mouse.y - origin.y);
        const Uint8 buttons[] = {SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT};

        for (int index = 0; index < 3; index++) {
            if (hovered && ImGui::IsMouseClicked(index)) {
                mouseHeld[index] = true;
                embeddedWindow->mouseDown(buttons[index], x, y);
            }
            if (mouseHeld[index] && ImGui::IsMouseDown(index)) {
                embeddedWindow->mouseMove(buttons[index], x, y);
            }
            if (mouseHeld[index] && ImGui::IsMouseReleased(index)) {
                mouseHeld[index] = false;
                embeddedWindow->mouseUp(buttons[index], x, y);
            }
        }

        if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
            embeddedWindow->callKeyDown(
                ImGui::GetIO().MouseWheel > 0.0f ? SDLK_ASTERISK : SDLK_SLASH);
        }
    }

    std::string getStatus()
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        return status;
    }

    GLuint colorTexture = 0;

private:
    void setStatus(std::string nextStatus)
    {
        std::lock_guard<std::mutex> lock(statusMutex);
        status = std::move(nextStatus);
    }

    bool resizeFramebuffer(int width, int height)
    {
        if (framebufferWidth == width && framebufferHeight == height) {
            return true;
        }

        if (depthBuffer != 0) {
            glDeleteRenderbuffers(1, &depthBuffer);
        }
        if (colorTexture != 0) {
            glDeleteTextures(1, &colorTexture);
        }
        if (framebuffer == 0) {
            glGenFramebuffers(1, &framebuffer);
        }

        glGenTextures(1, &colorTexture);
        glBindTexture(GL_TEXTURE_2D, colorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glGenRenderbuffers(1, &depthBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

        GLint previousFramebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTexture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, depthBuffer);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));

        framebufferWidth = width;
        framebufferHeight = height;
        if (!complete) {
            setStatus("GLVis could not create its panel framebuffer.");
        }
        return complete;
    }

    void destroySession()
    {
        if (session) {
            ReleaseVisualization(session->wnd.get());
            session.reset();
        }
        embeddedWindow = nullptr;
    }

    LoopbackServer server;
    std::thread listener;
    std::atomic<bool> stopping{false};
    std::mutex socketMutex;
    mfem::socketstream* activeSocket = nullptr;

    std::mutex stateMutex;
    std::optional<DataState> pendingState;
    std::unique_ptr<Window> session;
    ImGuiGlvisWindow* embeddedWindow = nullptr;
    bool inputFocused = false;

    std::mutex statusMutex;
    std::string status;

    GLuint framebuffer = 0;
    GLuint depthBuffer = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    GLfloat glvisClearColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
    bool mouseHeld[3]{};
};

GlvisAdapter::GlvisAdapter()
    : impl(std::make_unique<Impl>())
{
}

GlvisAdapter::~GlvisAdapter() = default;

void GlvisAdapter::processEvent(const SDL_Event& event)
{
    impl->processEvent(event);
}

void GlvisAdapter::draw()
{
    if (!ImGui::Begin("Visualization")) {
        impl->setInputFocused(false);
        ImGui::End();
        return;
    }

    ImGui::TextDisabled(
        impl->dimension() == 3 ? "3D"
        : impl->dimension() == 2 ? "2D"
        : "Waiting for mesh");
    ImGui::SameLine();
    if (ImGui::Button("Fit view")) {
        impl->fitView();
    }
    ImGui::SameLine();
    const std::string status = impl->getStatus();
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 imageSize(
        std::max(available.x, 1.0f),
        std::max(available.y, 1.0f));
    const int width = static_cast<int>(imageSize.x);
    const int height = static_cast<int>(imageSize.y);

    impl->updateSession(width, height);
    const bool rendered = impl->render(width, height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    if (rendered) {
        ImGui::Image(
            ImTextureRef(static_cast<ImTextureID>(impl->colorTexture)),
            imageSize,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
        const bool hovered = ImGui::IsItemHovered();
        impl->mouseInput(origin, hovered);
        impl->setInputFocused(
            hovered
            && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    }
    else {
        ImGui::InvisibleButton("##glvis-canvas", imageSize);
        impl->setInputFocused(false);
    }

    ImGui::End();
}

} // namespace App
