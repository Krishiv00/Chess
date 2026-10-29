#pragma once

#include <array>
#include <memory>
#include <functional>

#include "SFML/Graphics.hpp"

namespace Themes {
    class Theme {
    protected:
        std::array<sf::Color, 2> m_Checkerboard;
        std::array<sf::Color, 2> m_EvaluationBar;
        std::array<sf::Color, 5> m_Particles;
        sf::Color m_LastMoveHighlight;
        sf::Color m_LegalMoveHighlight;
        sf::Color m_MarkerHighlight;
        sf::Color m_Background;
        sf::Color m_SurfaceRaised;
        sf::Color m_SurfaceOverlay;
        sf::Color m_TextPrimary;
        sf::Color m_TextSecondary;
        sf::Color m_OverlayPill;
        sf::Color m_Arrow;

    public:
        virtual ~Theme() = default;

        virtual void RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const = 0;

        virtual void RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const = 0;

        [[nodiscard]]
        virtual sf::Color GetSelectedPiece() const = 0;

        [[nodiscard]]
        virtual bool DrawLastMoveTargetIfCapturable() const noexcept = 0;

        [[nodiscard]]
        virtual bool DrawSelectedPieceGhost() const noexcept = 0;

        [[nodiscard]]
        virtual bool DrawKnightArrowAsL() const noexcept = 0;

        [[nodiscard]]
        virtual bool DrawHoveredLegalSq() const noexcept = 0;

        [[nodiscard]]
        virtual bool DrawHoveredSqOutline() const noexcept = 0;

        [[nodiscard]]
        virtual bool DrawIllegalMoveHighlight() const noexcept = 0;

        [[nodiscard]]
        inline const std::array<sf::Color, 2>& GetCheckerboard() const noexcept {
            return m_Checkerboard;
        }

        [[nodiscard]]
        inline const std::array<sf::Color, 2>& GetEvaluationBar() const noexcept {
            return m_EvaluationBar;
        }

        [[nodiscard]]
        inline const std::array<sf::Color, 5>& GetParticles() const noexcept {
            return m_Particles;
        }

        [[nodiscard]]
        inline sf::Color GetLastMoveHighlight() const noexcept {
            return m_LastMoveHighlight;
        }

        [[nodiscard]]
        inline sf::Color GetLegalMoveHighlight() const noexcept {
            return m_LegalMoveHighlight;
        }

        [[nodiscard]]
        inline sf::Color GetMarkerHighlight() const noexcept {
            return m_MarkerHighlight;
        }

        [[nodiscard]]
        inline sf::Color GetBackground() const noexcept {
            return m_Background;
        }

        [[nodiscard]]
        inline sf::Color GetSurfaceRaised() const noexcept {
            return m_SurfaceRaised;
        }

        [[nodiscard]]
        inline sf::Color GetSurfaceOverlay() const noexcept {
            return m_SurfaceOverlay;
        }

        [[nodiscard]]
        inline sf::Color GetTextPrimary() const noexcept {
            return m_TextPrimary;
        }

        [[nodiscard]]
        inline sf::Color GetTextSecondary() const noexcept {
            return m_TextSecondary;
        }

        [[nodiscard]]
        inline sf::Color GetOverlayPill() const noexcept {
            return m_OverlayPill;
        }

        [[nodiscard]]
        inline sf::Color GetArrow() const noexcept {
            return m_Arrow;
        }
    };

    class Chesscom : public Theme {
    public:
        Chesscom();

        virtual void RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const override;

        virtual void RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const override;

        [[nodiscard]]
        virtual sf::Color GetSelectedPiece() const override;

        [[nodiscard]]
        virtual bool DrawLastMoveTargetIfCapturable() const noexcept override {
            return true;
        }

        [[nodiscard]]
        virtual bool DrawSelectedPieceGhost() const noexcept override {
            return false;
        }

        [[nodiscard]]
        virtual bool DrawKnightArrowAsL() const noexcept override {
            return true;
        }

        [[nodiscard]]
        virtual bool DrawHoveredLegalSq() const noexcept override {
            return false;
        }

        [[nodiscard]]
        virtual bool DrawHoveredSqOutline() const noexcept override {
            return true;
        }

        [[nodiscard]]
        virtual bool DrawIllegalMoveHighlight() const noexcept override {
            return true;
        }
    };

    class ChesscomGlass final : public Chesscom {
    public:
        ChesscomGlass();
    };

    class ChesscomBrown final : public Chesscom {
    public:
        ChesscomBrown();
    };

    class ChesscomSky final : public Chesscom {
    public:
        ChesscomSky();
    };

    class Chesscom8Bit final : public Chesscom {
    public:
        Chesscom8Bit();
    };

    class ChesscomPurple final : public Chesscom {
    public:
        ChesscomPurple();
    };

    class ChesscomLight final : public Chesscom {
    public:
        ChesscomLight();
    };

    class ChesscomDark final : public Chesscom {
    public:
        ChesscomDark();
    };

    class ChesscomDarkBlue final : public Chesscom {
    public:
        ChesscomDarkBlue();
    };

    class ChesscomBubblegum final : public Chesscom {
    public:
        ChesscomBubblegum();
    };

    class ChesscomCheckers final : public Chesscom {
    public:
        ChesscomCheckers();
    };

    class ChesscomOrange final : public Chesscom {
    public:
        ChesscomOrange();
    };

    class ChesscomRed final : public Chesscom {
    public:
        ChesscomRed();
    };

    class ChesscomTan final : public Chesscom {
    public:
        ChesscomTan();
    };

    class ChesscomBlue final : public Chesscom {
    public:
        ChesscomBlue();
    };

    class ChesscomPink final : public Chesscom {
    public:
        ChesscomPink();
    };

    class ChesscomTheMusical final : public Chesscom {
    public:
        ChesscomTheMusical();
    };

    class Lichess : public Theme {
    public:
        Lichess();

        virtual void RenderMarker(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const override;

        virtual void RenderCaptureOverlay(sf::RenderTarget& target, sf::Vector2f position, float squareSize) const override;

        [[nodiscard]]
        virtual sf::Color GetSelectedPiece() const override;

        [[nodiscard]]
        virtual bool DrawLastMoveTargetIfCapturable() const noexcept override {
            return false;
        }

        [[nodiscard]]
        virtual bool DrawSelectedPieceGhost() const noexcept override {
            return true;
        }

        [[nodiscard]]
        virtual bool DrawKnightArrowAsL() const noexcept override {
            return false;
        }

        [[nodiscard]]
        virtual bool DrawHoveredLegalSq() const noexcept override {
            return true;
        }

        [[nodiscard]]
        virtual bool DrawHoveredSqOutline() const noexcept override {
            return false;
        }

        [[nodiscard]]
        virtual bool DrawIllegalMoveHighlight() const noexcept override {
            return false;
        }
    };

    class LichessBlue final : public Lichess {
    public:
        LichessBlue();
    };

    class LichessGreen final : public Lichess {
    public:
        LichessGreen();
    };

    class LichessDark final : public Lichess {
    public:
        LichessDark();
    };

    class LichessPurple final : public Lichess {
    public:
        LichessPurple();
    };

    inline const std::function<std::unique_ptr<Themes::Theme>()> Factories[] = {
        []() { return std::make_unique<Themes::Chesscom>(); },
        []() { return std::make_unique<Themes::Lichess>(); },
        []() { return std::make_unique<Themes::ChesscomGlass>(); },
        []() { return std::make_unique<Themes::LichessBlue>(); },
        []() { return std::make_unique<Themes::ChesscomBrown>(); },
        []() { return std::make_unique<Themes::LichessGreen>(); },
        []() { return std::make_unique<Themes::ChesscomSky>(); },
        []() { return std::make_unique<Themes::LichessDark>(); },
        []() { return std::make_unique<Themes::Chesscom8Bit>(); },
        []() { return std::make_unique<Themes::LichessPurple>(); },
        []() { return std::make_unique<Themes::ChesscomPurple>(); },
        []() { return std::make_unique<Themes::ChesscomLight>(); },
        []() { return std::make_unique<Themes::ChesscomDark>(); },
        []() { return std::make_unique<Themes::ChesscomDarkBlue>(); },
        []() { return std::make_unique<Themes::ChesscomBubblegum>(); },
        []() { return std::make_unique<Themes::ChesscomCheckers>(); },
        []() { return std::make_unique<Themes::ChesscomOrange>(); },
        []() { return std::make_unique<Themes::ChesscomRed>(); },
        []() { return std::make_unique<Themes::ChesscomTan>(); },
        []() { return std::make_unique<Themes::ChesscomBlue>(); },
        []() { return std::make_unique<Themes::ChesscomPink>(); },
        []() { return std::make_unique<Themes::ChesscomTheMusical>(); },
    };

    constexpr inline const std::string_view Names[] = {
        "Chess.com Default",
        "Lichess Default",
        "Chess.com Glass",
        "Lichess Blue",
        "Chess.com Brown",
        "Lichess Green",
        "Chess.com Sky",
        "Lichess Dark",
        "Chess.com 8-Bit",
        "Lichess Purple",
        "Chess.com Purple",
        "Chess.com Light",
        "Chess.com Dark",
        "Chess.com Dark Blue",
        "Chess.com Bubblegum",
        "Chess.com Checkers",
        "Chess.com Orange",
        "Chess.com Red",
        "Chess.com Tan",
        "Chess.com Blue",
        "Chess.com Pink",
        "Chess.com The Musical",
    };

    constexpr inline const std::size_t Count = sizeof(Factories) / sizeof(Factories[0]);

    static_assert(Count == sizeof(Names) / sizeof(Names[0]), "Theme count mismatch!");
}
