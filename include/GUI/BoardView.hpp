#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "SFML/Graphics.hpp"

#include "GUI/Game/Game.hpp"
#include "GUI/Themes.hpp"
#include "GUI/UI/UI.hpp"
#include "GUI/UI/Widgets.hpp"

class BoardView final {
private:
    [[nodiscard]]
    inline int mapRank(int rank) const noexcept {
        return m_Flipped ? (Chess::Ranks - 1 - rank) : rank;
    }

    [[nodiscard]]
    inline int mapFile(int file) const noexcept {
        return m_Flipped ? (Chess::Files - 1 - file) : file;
    }

    [[nodiscard]]
    inline bool hasSelection() const noexcept {
        return m_Selected != Chess::NullPos;
    }

    [[nodiscard]]
    std::optional<int> squareAt(sf::Vector2i position) const noexcept;

    [[nodiscard]]
    sf::Vector2f squarePosition(int idx) const noexcept;

    [[nodiscard]]
    int hitTestPromotionMenu(sf::Vector2i position) const noexcept;

    void select(int idx);
    void clearSelection();
    void signal(int idx, bool released);
    void drop(int idx, bool animate, bool playIllegalSfx);
    void commitPromotion(Chess::MoveFlag flag);
    void toggleArrow(uint8_t start, uint8_t end);
    void rebuildBoard();

    void renderBoard(const UI::Painter& painter) const;
    void renderHighlights(const UI::Painter& painter, sf::Vector2i mouse) const;
    void appendPiece(std::vector<sf::Vertex>& out, Chess::Piece piece, float x, float y, float angle = 0.f) const;
    void renderPiece(sf::RenderTarget& target, Chess::Piece piece, float x, float y, float angle = 0.f) const;
    void rebuildCoordinates(const sf::Font& font) const;
    void renderPieces(const UI::Painter& painter, bool mouseHeld) const;
    void renderArrow(sf::RenderTarget& target, Arrow arrow) const;
    void renderPromotionMenu(const UI::Painter& painter) const;
    void renderGameOver(const UI::Painter& painter) const;

    const Game& m_Game;
    const sf::Texture& m_Pieces;
    const Themes::Theme* m_Theme{nullptr};

    sf::Vector2f m_Origin;
    float m_Square{0.f};
    sf::VertexArray m_BoardVertices{sf::PrimitiveType::Triangles};

    // Built lazily on the first render
    mutable std::vector<sf::Text> m_Coordinates;
    mutable bool m_CoordinatesDirty{true};

    mutable std::vector<sf::Vertex> m_PieceBatch;

    bool m_Flipped{false};
    bool m_Interactive{false};
    bool m_ShowCoordinates{true};

    // Invariant: only valid when not equal to `Chess::NullPos`
    uint8_t m_Selected{Chess::NullPos};
    std::vector<Chess::Move> m_LegalMoves;

    bool m_PromotionActive{false};
    uint8_t m_PromotionTarget{Chess::NullPos};

    uint64_t m_Markers{0ull};
    std::vector<Arrow> m_Arrows;
    Arrow m_DrawingArrow;

    std::vector<PieceMoveAnim> m_Animations;

    float m_DragTilt{0.f};
    float m_InspectionT{0.f};
    bool m_Inspection{false};

    float m_IllegalTimer{0.f};
    uint8_t m_IllegalSquare{Chess::NullPos};

    float m_GameOverTimer{0.f};
    std::string m_GameOverTitle;
    std::string m_GameOverSubtitle;
    ParticleSystem m_Particles;

    sf::Vector2i m_LastMouse;

public:
    BoardView(const Game& game, const sf::Texture& pieces) : m_Game(game), m_Pieces(pieces) {}

    std::function<void(Chess::Move, bool animate)> OnMove;
    std::function<void()> OnIllegalMove;
    std::function<void()> OnPromotionPrompt;

    void SetGeometry(sf::Vector2f origin, float squareSize);
    void SetTheme(const Themes::Theme& theme);

    inline void SetInteractive(bool interactive) {
        m_Interactive = interactive;

        if (!interactive && !m_PromotionActive) clearSelection();
    }

    inline void SetInspection(bool inspection) noexcept {
        m_Inspection = inspection;
    }

    inline void SetShowCoordinates(bool show) noexcept {
        m_ShowCoordinates = show;
    }

    inline void SetFlipped(bool flipped) noexcept {
        if (m_Flipped != flipped) m_CoordinatesDirty = true;

        m_Flipped = flipped;
    }

    [[nodiscard]]
    inline bool Flipped() const noexcept {
        return m_Flipped;
    }

    void NewGame();

    void ClearAnnotations();
    void AddArrow(uint8_t start, uint8_t end);

    void AnimateMove(Chess::Move move, bool animate);

    void ShowGameOver(std::string title, std::string subtitle, bool confetti);

    void Update(float deltaTime);
    void Render(const UI::Painter& painter, sf::Vector2i mouse) const;

    void HandlePressed(sf::Mouse::Button button, sf::Vector2i position);
    void HandleReleased(sf::Mouse::Button button, sf::Vector2i position);
    void HandleMoved(sf::Vector2i position);
};
