#include "GUI/Launcher.hpp"

#include <algorithm>
#include <cmath>

#include "GUI/Settings.hpp"

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#elif defined(__linux__) && __has_include(<X11/Xlib.h>)
    #include <dlfcn.h>
    #include <X11/Xatom.h>
    #include <X11/Xlib.h>
    #define CHESS_X11_WINDOWS
#endif

namespace {
    constexpr float MinAspect = 1.39f;
    constexpr unsigned int MinHeight = 480;
    constexpr float StateDelay = 0.15f;

    [[nodiscard]]
    sf::Vector2u ClampSize(sf::Vector2u size) {
        const sf::Vector2u desktop = sf::VideoMode::getDesktopMode().size;

        unsigned int height = std::clamp(size.y, MinHeight, std::max(MinHeight, desktop.y));
        unsigned int width = std::max(size.x, static_cast<unsigned int>(std::ceil(MinAspect * static_cast<float>(height))));

        if (width > desktop.x) {
            width = desktop.x;
            height = std::min(height, static_cast<unsigned int>(static_cast<float>(width) / MinAspect));
        }

        return sf::Vector2u(width, std::max(1u, height));
    }

#if defined(_WIN32)
    bool IsMaximized(sf::WindowHandle handle) {
        return IsZoomed(static_cast<HWND>(handle)) != 0;
    }

    void Maximize(sf::WindowHandle handle) {
        ShowWindow(static_cast<HWND>(handle), SW_MAXIMIZE);
    }
#elif defined(CHESS_X11_WINDOWS)
    class X11 final {
    private:
        using OpenDisplayFn = Display* (*)(const char*);
        using InternAtomFn = Atom (*)(Display*, const char*, Bool);
        using GetPropertyFn = int (*)(Display*, Window, Atom, long, long, Bool, Atom, Atom*, int*, unsigned long*, unsigned long*, unsigned char**);
        using SendEventFn = Status (*)(Display*, Window, Bool, long, XEvent*);
        using RootWindowFn = Window (*)(Display*);
        using FlushFn = int (*)(Display*);
        using FreeFn = int (*)(void*);

        Display* m_Display{nullptr};
        InternAtomFn m_InternAtom{nullptr};
        GetPropertyFn m_GetProperty{nullptr};
        SendEventFn m_SendEvent{nullptr};
        RootWindowFn m_RootWindow{nullptr};
        FlushFn m_Flush{nullptr};
        FreeFn m_Free{nullptr};

        X11() {
            void* lib = dlopen("libX11.so.6", RTLD_LAZY);

            if (!lib) return;

            const auto open = reinterpret_cast<OpenDisplayFn>(dlsym(lib, "XOpenDisplay"));
            m_InternAtom = reinterpret_cast<InternAtomFn>(dlsym(lib, "XInternAtom"));
            m_GetProperty = reinterpret_cast<GetPropertyFn>(dlsym(lib, "XGetWindowProperty"));
            m_SendEvent = reinterpret_cast<SendEventFn>(dlsym(lib, "XSendEvent"));
            m_RootWindow = reinterpret_cast<RootWindowFn>(dlsym(lib, "XDefaultRootWindow"));
            m_Flush = reinterpret_cast<FlushFn>(dlsym(lib, "XFlush"));
            m_Free = reinterpret_cast<FreeFn>(dlsym(lib, "XFree"));

            if (open && m_InternAtom && m_GetProperty && m_SendEvent && m_RootWindow && m_Flush && m_Free) {
                m_Display = open(nullptr);
            }
        }

    public:
        static X11& Get() {
            static X11 instance;
            return instance;
        }

        bool IsMaximized(Window window) const {
            if (!m_Display) return false;

            const Atom state = m_InternAtom(m_Display, "_NET_WM_STATE", False);
            const Atom horizontal = m_InternAtom(m_Display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
            const Atom vertical = m_InternAtom(m_Display, "_NET_WM_STATE_MAXIMIZED_VERT", False);

            Atom type;
            int format;
            unsigned long count = 0, remaining = 0;
            unsigned char* data = nullptr;

            if (m_GetProperty(m_Display, window, state, 0, 1024, False, XA_ATOM, &type, &format, &count, &remaining, &data) != Success || !data) {
                return false;
            }

            bool horz = false, vert = false;
            const Atom* atoms = reinterpret_cast<const Atom*>(data);

            for (unsigned long i = 0; i < count; ++i) {
                horz |= atoms[i] == horizontal;
                vert |= atoms[i] == vertical;
            }

            m_Free(data);

            return horz && vert;
        }

        void Maximize(Window window) const {
            if (!m_Display) return;

            XEvent event{};
            event.xclient.type = ClientMessage;
            event.xclient.window = window;
            event.xclient.message_type = m_InternAtom(m_Display, "_NET_WM_STATE", False);
            event.xclient.format = 32;
            event.xclient.data.l[0] = 1;
            event.xclient.data.l[1] = static_cast<long>(m_InternAtom(m_Display, "_NET_WM_STATE_MAXIMIZED_HORZ", False));
            event.xclient.data.l[2] = static_cast<long>(m_InternAtom(m_Display, "_NET_WM_STATE_MAXIMIZED_VERT", False));
            event.xclient.data.l[3] = 1;

            m_SendEvent(m_Display, m_RootWindow(m_Display), False, SubstructureNotifyMask | SubstructureRedirectMask, &event);
            m_Flush(m_Display);
        }
    };

    bool IsMaximized(sf::WindowHandle handle) {
        return X11::Get().IsMaximized(static_cast<Window>(handle));
    }

    void Maximize(sf::WindowHandle handle) {
        X11::Get().Maximize(static_cast<Window>(handle));
    }
#else
    bool IsMaximized(sf::WindowHandle) {
        return false;
    }

    void Maximize(sf::WindowHandle) {}
#endif
}

Launcher::Launcher(const Config& config) {
    Settings saved;
    saved.Load(SettingsFile);

    const sf::Vector2u defaultSize(
        static_cast<unsigned int>(std::ceil(MinAspect * static_cast<float>(config.WindowSize))), config.WindowSize
    );

    const bool hasSavedSize = saved.WindowWidth > 0 && saved.WindowHeight > 0;

    m_RestoredSize = ClampSize(
        hasSavedSize ? sf::Vector2u(static_cast<unsigned int>(saved.WindowWidth), static_cast<unsigned int>(saved.WindowHeight)) : defaultSize
    );

    m_Window.create(
        sf::VideoMode(m_RestoredSize),
        config.WindowTitle,
        config.WindowStyle,
        sf::State::Windowed
    );

    m_Window.setMinimumSize(sf::Vector2u(static_cast<unsigned int>(std::ceil(MinAspect * MinHeight)), MinHeight));
    m_Window.setMaximumSize(sf::VideoMode::getDesktopMode().size);

    m_Window.clear();
    m_Window.display();

    m_Window.setFramerateLimit(config.Fps);

    if (saved.WindowMaximized) Maximize(m_Window.getNativeHandle());

    m_Application.SetTargetSize(m_Window.getSize());
}

void Launcher::Update() {
    const float deltaTime = std::min(1.f / 60.f, m_Clock.restart().asSeconds());

    if (m_StateTimer > 0.f && (m_StateTimer -= deltaTime) <= 0.f) commitWindowState();

    m_Application.Update(deltaTime, sf::Mouse::getPosition(m_Window));
}

void Launcher::Render() const {
    m_Window.clear();

    const sf::Vector2i mousePosition = sf::Mouse::getPosition(m_Window);

    m_Application.Render(m_Window, mousePosition);

    m_Window.display();
}

void Launcher::HandleEvents() {
    while (const std::optional<sf::Event> event = m_Window.pollEvent()) {
        onEvent(event.value());
    }
}

bool Launcher::LoadResources(const std::filesystem::path& root) {
    if (!m_Application.LoadResources(root)) [[unlikely]] {
        m_Window.close();

        return false;
    }

    m_Window.setIcon(sf::Image(root / "icon.png"));

    return true;
}

void Launcher::onResized(sf::Vector2u size) {
    if (size.x == 0 || size.y == 0) return;

    if (!IsMaximized(m_Window.getNativeHandle())) {
        const sf::Vector2u clamped = ClampSize(size);

        if (clamped != size) {
            m_Window.setSize(clamped);
            size = clamped;
        }
    }

    m_Window.setView(sf::View(sf::FloatRect(sf::Vector2f(), sf::Vector2f(size))));

    m_Application.SetTargetSize(size);
    m_Clock.restart();

    m_StateTimer = StateDelay;
}

void Launcher::commitWindowState() {
    const bool maximized = IsMaximized(m_Window.getNativeHandle());

    if (!maximized) m_RestoredSize = m_Window.getSize();

    m_Application.SetWindowState(m_RestoredSize, maximized);
}

void Launcher::onEvent(const sf::Event& event) {
    if (event.is<sf::Event::Closed>()) {
        m_Window.close();
    }

    else if (event.is<sf::Event::MouseEntered>()) {
        if (auto handCursor = sf::Cursor::createFromSystem(sf::Cursor::Type::Hand)) {
            m_Window.setMouseCursor(handCursor.value());
        }
    }

    else if (const auto* resize = event.getIf<sf::Event::Resized>()) {
        onResized(resize->size);
    }

    else if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        // Escape closes the window unless it had something to dismiss
        if (!m_Application.HandleKeyPressed(key->scancode, key->control)) {
            m_Window.close();
        }
    }

    else if (const auto* text = event.getIf<sf::Event::TextEntered>()) {
        m_Application.HandleTextEntered(text->unicode);
    }

    else if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        m_Application.HandleMouseWheel(wheel->position, wheel->delta);
    }

    else if (const auto* button = event.getIf<sf::Event::MouseButtonPressed>()) {
        m_Application.HandleMouseButtonPressed(button->button, button->position);
    }

    else if (const auto* button = event.getIf<sf::Event::MouseButtonReleased>()) {
        m_Application.HandleMouseButtonReleased(button->button, button->position);
    }

    else if (const auto* move = event.getIf<sf::Event::MouseMoved>()) {
        m_Application.HandleMouseMoved(move->position);
    }
}
