#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "SFML/Graphics.hpp"

namespace Utils {
    [[nodiscard]]
    inline float ExponentiallyMoveTo(float from, float to, float speed, float snapThreshold = 0.002f) noexcept {
        const float diff = to - from;
        return std::fabs(diff) < snapThreshold ? to : (from + diff * (1.f - std::exp(-speed)));
    }

    [[nodiscard]]
    constexpr inline sf::Color ConvertAlpha(sf::Color color, float factor) noexcept {
        color.a = static_cast<uint8_t>(color.a * factor);
        return color;
    }

    void RenderQuad(
        sf::RenderTarget& target,
        sf::Color color,
        sf::Vector2f position,
        sf::Vector2f size
    );

    void RenderRoundedRect(
        sf::RenderTarget& target,
        sf::Color color,
        sf::Vector2f position,
        sf::Vector2f size,
        float radius
    );

    void RenderRoundedQuad(
        sf::RenderTarget& target,
        sf::Color color,
        sf::Vector2f position,
        sf::Vector2f size,
        float roundness
    );

    void RenderRoundedFrame(
        sf::RenderTarget& target,
        sf::Color color,
        sf::Vector2f position,
        sf::Vector2f size,
        float radius,
        float thickness
    );

    void RenderFrame(
        sf::RenderTarget& target,
        sf::Color color,
        sf::Vector2f position,
        sf::Vector2f size,
        float thicknessFactor
    );
}

struct PieceMoveAnim {
    float Timer{0.f};
    uint8_t Start{0}, End{0};

    PieceMoveAnim() = default;
    PieceMoveAnim(uint8_t start, uint8_t end) : Timer(1.f), Start(start), End(end) {}
};

struct Arrow {
    static constexpr inline const uint8_t NullPos = 64;

    uint8_t Start{NullPos};
    uint8_t End{NullPos};

    [[nodiscard]]
    inline operator bool() const noexcept {
        return Start != NullPos && End != NullPos && Start != End;
    }
};

class ParticleSystem final {
public:
    struct Particle {
        enum class Shapes : uint8_t {
            Square, Diamond, Circle
        };

        sf::Vector2f Position;
        sf::Vector2f Velocity;
        float Rotation{0.f};
        float RotationSpeed{0.f};
        float Size{0.f};
        float Life{1.f};
        uint8_t ColorIndex{0};
        Shapes Shape{Shapes::Square};
    };

private:
    std::vector<Particle> m_Particles;

public:
    void RemoveAll();

    void Spawn(std::size_t count, sf::FloatRect area);

    void Update(float deltaTime);

    void Render(
        sf::RenderTarget& target, const std::array<sf::Color, 5>& colors, float opacityFactor
    ) const;

    [[nodiscard]]
    inline const std::vector<Particle>& GetParticles() const noexcept {
        return m_Particles;
    }
};

class EvaluationBar final {
private:
    float m_CurrentEvaluation{0.5f};
    float m_LatestEvaluation{0.5f};

    mutable std::optional<sf::RenderTexture> m_Texture;
    mutable sf::Vector2u m_TextureSize;
    mutable float m_DrawnMid{-1.f};
    mutable bool m_DrawnFlipped{false};
    mutable std::array<sf::Color, 2> m_DrawnColors;

public:
    void SetEval(float eval);

    [[nodiscard]]
    inline float GetEval() const noexcept {
        return m_LatestEvaluation;
    }

    void Update(float deltaTime);

    void Render(
        sf::RenderTarget& target, sf::Vector2f position, sf::Vector2f size,
        bool flipped, const std::array<sf::Color, 2>& colors
    ) const;
};
