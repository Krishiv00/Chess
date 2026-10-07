#pragma once

#include <optional>
#include <filesystem>

#include "SFML/Audio.hpp"

enum class Sfx {
    Capture,
    Check,
    GameEnd,
    Promotion,
    Notify,
    MoveSelf,
    MoveOpponent,
    SpecialMove,
    Illegal,
    Count
};

class SfxPlayer final {
private:
    std::optional<sf::Sound> m_Sounds[static_cast<std::size_t>(Sfx::Count)];
    sf::SoundBuffer m_Buffers[static_cast<std::size_t>(Sfx::Count)];

public:
    [[nodiscard]]
    inline bool LoadFromFile(Sfx sfx, const std::filesystem::path& filepath) {
        const std::size_t index = static_cast<std::size_t>(sfx);

        sf::SoundBuffer& buffer = m_Buffers[index];

        if (!buffer.loadFromFile(filepath)) [[unlikely]] {
            return false;
        }

        m_Sounds[index].emplace(buffer);

        return true;
    }

    inline void Play(Sfx sfx) {
        if (std::optional<sf::Sound>& sound = m_Sounds[static_cast<std::size_t>(sfx)]) [[likely]] {
            sound->play();
        }
    }
};