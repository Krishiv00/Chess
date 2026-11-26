#include "GUI/Launcher.hpp"

int main() {
    Launcher launcher({
        .WindowSize = 800u,
        .Fps = 60u,
        .WindowTitle = "Chess",
        .WindowStyle = sf::Style::Default
    });

    if (!launcher.LoadResources("Resources/")) [[unlikely]] {
        return 1;
    }

    while (launcher.isRunning()) {
        launcher.HandleEvents();
        launcher.Update();
        launcher.Render();
    }

    return 0;
}