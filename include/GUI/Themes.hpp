#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "SFML/Graphics.hpp"

namespace Themes {
    class Theme {
    protected:
        std::string_view m_Name;
        std::array<sf::Color, 2> m_Checkerboard;
        std::array<sf::Color, 2> m_EvaluationBar;
        std::array<sf::Color, 5> m_Particles;
        sf::Color m_LastMoveHighlight;
        sf::Color m_LegalMoveHighlight;
        sf::Color m_MarkerHighlight;
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
        inline std::string_view GetName() const noexcept {
            return m_Name;
        }

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

    using Factory = std::unique_ptr<Theme>(*)();

    template <class T>
    std::unique_ptr<Theme> Make() {
        return std::make_unique<T>();
    }

    inline constexpr const Factory Factories[] = {
        Make<Chesscom>,
        Make<Lichess>,
        Make<ChesscomGlass>,
        Make<LichessBlue>,
        Make<ChesscomBrown>,
        Make<LichessGreen>,
        Make<ChesscomSky>,
        Make<LichessDark>,
        Make<Chesscom8Bit>,
        Make<LichessPurple>,
        Make<ChesscomPurple>,
        Make<ChesscomLight>,
        Make<ChesscomDark>,
        Make<ChesscomDarkBlue>,
        Make<ChesscomBubblegum>,
        Make<ChesscomCheckers>,
        Make<ChesscomOrange>,
        Make<ChesscomRed>,
        Make<ChesscomTan>,
        Make<ChesscomBlue>,
        Make<ChesscomPink>,
        Make<ChesscomTheMusical>,
    };

    constexpr inline const std::size_t Count = sizeof(Factories) / sizeof(Factories[0]);

    struct Info {
        std::string Name;
        std::array<sf::Color, 2> Checkerboard;
    };

    [[nodiscard]]
    const std::vector<Info>& List();

    [[nodiscard]]
    std::unique_ptr<Theme> Create(std::size_t index);

    [[nodiscard]]
    std::size_t IndexOf(std::string_view name);
}
