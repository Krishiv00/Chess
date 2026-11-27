#pragma once

#include <cstdint>
#include <filesystem>
#include <string>


namespace Platform {
    [[nodiscard]]
    unsigned int TotalThreads() noexcept;

    [[nodiscard]]
    unsigned int DefaultThreads() noexcept;

    [[nodiscard]]
    unsigned int MaxHashMB() noexcept;
}

inline constexpr const char* SettingsFile = "Settings.cfg";

enum class GameMode : uint8_t {
    Engine,
    Local,
    Online
};

namespace Limits {
    constexpr inline const int ThinkTimeMin = 50; // ms
    constexpr inline const int ThinkTimeMax = 120000;
    constexpr inline const int ThinkTimeStep = 50;
    constexpr inline const int DepthMin = 1;
    constexpr inline const int DepthMax = 20;
    constexpr inline const int VolumeMin = 0;
    constexpr inline const int VolumeMax = 100;
}

struct Settings {
    // Engine
    bool LimitByDepth{false};
    int ThinkTimeMs{250};
    int SearchDepth{12};
    int EvalDepth{12};
    bool OwnBook{true};
    bool Ponder{false};
    bool CatchAll{false};
    int HashMB{128};
    int Threads{static_cast<int>(Platform::DefaultThreads())};

    // Display
    std::string Theme;
    std::string PieceSet{"chesscom"};
    bool ShowEvalBar{true};
    bool ShowMoveList{true};
    bool ShowCoordinates{true};

    // Sound
    bool SoundEnabled{true};
    int Volume{100};

    // Online
    std::string PlayerName;

    // Window
    int WindowWidth{0};
    int WindowHeight{0};
    bool WindowMaximized{false};

    void Load(const std::filesystem::path& path);
    void Save(const std::filesystem::path& path) const;

private:
    void clamp();
};
