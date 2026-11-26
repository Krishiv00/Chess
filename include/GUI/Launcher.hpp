#pragma once

#include <filesystem>

#include "SFML/Graphics.hpp"

#include "GUI/Application.hpp"

class Launcher final {
public:
    struct Config {
        unsigned int WindowSize; // default window height, the width follows from it
        unsigned int Fps;
        const char* WindowTitle;
        unsigned int WindowStyle;
    };

private:
    void onResized(sf::Vector2u size);
    void commitWindowState();
    void onEvent(const sf::Event& event);

    mutable sf::RenderWindow m_Window;

    sf::Clock m_Clock;

    Application m_Application;

    sf::Vector2u m_RestoredSize;
    float m_StateTimer{0.f};

public:
    Launcher(const Config& config);

    [[nodiscard]]
    bool LoadResources(const std::filesystem::path& root);

    void HandleEvents();
    void Update();
    void Render() const;

    [[nodiscard]]
    inline bool isRunning() const noexcept {
        return m_Window.isOpen();
    }
};
