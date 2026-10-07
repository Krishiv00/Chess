#pragma once

#include <vector>
#include <filesystem>
#include <thread>
#include <memory>

#include "SFML/Graphics.hpp"

#include "Engine/Chess.hpp"

#include "GUI/Themes.hpp"
#include "GUI/Widgets.hpp"
#include "GUI/SoundSystem.hpp"

class Application final {
private:
    enum class GameOverResult {
        None,
        Checkmate,
        Stalemate,
        DrawFiftyMove,
        DrawRepetition,
        DrawInsufficient
    };

    struct GameState {
        std::string Fen{Chess::DefaultFEN};
        Chess::Move LastMove;
    };

    [[nodiscard]]
    inline bool hasSelectedPiece() const noexcept {
        return m_SelectedPiece != Chess::NullPos;
    }

    [[nodiscard]]
    inline int mapRank(int rank) const noexcept {
        return m_Flipped ? (Chess::Ranks - 1 - rank) : rank;
    }

    [[nodiscard]]
    inline int mapFile(int file) const noexcept {
        return m_Flipped ? (Chess::Files - 1 - file) : file;
    }

    [[nodiscard]]
    std::pair<int, int> mapMousePosToCoordinates(sf::Vector2i position) const noexcept {
        return std::make_pair(
            mapRank(position.y / m_SquareSize),
            mapFile((position.x - m_EvaluationBarWidth) / m_SquareSize)
        );
    }

    void joinThreads();

    void startPonder();
    void stopPonder();

    void loadFen(const std::string& fen);

    void doMove(Chess::Move move, bool animate);
    void onMouseButtonSignal(sf::Vector2i position, bool released);
    void pickPiece(int idx);
    void dropPiece(int idx, bool animate = false);

    void commitPromotion(Chess::MoveFlag promotionFlag);
    int hitTestPromotionMenu(sf::Vector2i mousePos) const;

    void pollEngineMove(bool suggestOnly = false);
    void updateEvaluation();

    [[nodiscard]]
    std::vector<Arrow>::iterator findArrow(uint8_t start, uint8_t end);

    void toggleArrow(uint8_t start, uint8_t end);
    void addArrow(uint8_t start, uint8_t end);

    void renderBoard(sf::RenderTarget& target) const;
    void renderSquareHighlight(sf::RenderTarget& target, uint8_t square, sf::Color color) const;
    void renderPiece(sf::RenderTarget& target, Chess::Piece piece, float x, float y, float angle = 0.f) const;
    void renderPieces(sf::RenderTarget& target, bool mouseHeld) const;
    void renderLegalMoves(sf::RenderTarget& target, sf::Vector2i mousePosition) const;
    void renderPopup(sf::RenderTarget& target) const;
    void renderCheckmateOverlay(sf::RenderTarget& target) const;
    void renderPromotionMenu(sf::RenderTarget& target, sf::Vector2i mousePos) const;
    void renderArrow(sf::RenderTarget& target, Arrow arrow) const;

    Chess::Board m_Board;
    Chess::Move m_LastMove;
    Chess::PieceColor m_SideToMove;
    Chess::PieceColor m_LastEngineColor;

    uint16_t m_EngineThinkTimeMs{250};
    uint8_t m_EvaluationDepth{12};
    bool m_UseOwnBook{true};
    bool m_Ponder{false};

    // Invariant: only valid when not equal to `Chess::NullPos`
    uint8_t m_SelectedPiece{Chess::NullPos};
    std::vector<Chess::Move> m_LegalMovesForSelectedPiece;

    float m_EvaluationBarWidth;
    float m_SquareSize;
    float m_PanelWidth;
    float m_InspectionModeOverlay_t{0.f};
    float m_DragTilt{0.f};

    float m_GameOverTimer{0.f};
    GameOverResult m_GameOverResult{GameOverResult::None};
    ParticleSystem m_ParticleSystem;

    GameState m_InspectionEntryState;

    sf::Vector2i m_LastMousePosition;

    // Multithreading
    std::thread m_SearchThread;
    std::thread m_PonderThread;
    Chess::Move m_PendingEngineMove;

    // Game state
    bool m_GameOver{false};
    bool m_Flipped{false};
    bool m_PromotionSelectionActive{false};
    bool m_EngineThinking{false};
    bool m_InspectionMode{false};
    bool m_SuggestingMove{false};

    // UI / UX
    std::size_t m_CurrentThemeIdx{0};
    std::unique_ptr<Themes::Theme> m_Theme{Themes::Factories[m_CurrentThemeIdx]()};
    std::vector<PieceMoveAnim> m_PieceAnimations;
    std::vector<Arrow> m_Arrows;
    Popup m_Popup;
    EvaluationBar m_EvaluationBar;
    ButtonPanel m_ButtonPanel;
    uint64_t m_Markers{0ull};
    Arrow m_CurrentlyDrawingArrow;

    // Resources
    sf::Texture m_PieceTexture;
    sf::Texture m_IconTexture;
    sf::Font m_Font;

    SfxPlayer m_SfxPlayer;

public:
    Application();
    ~Application();

    void SetTargetSize(sf::Vector2u size);

    [[nodiscard]]
    bool LoadResources(const std::filesystem::path& root);

    void Update(float deltaTime);
    void Render(sf::RenderTarget& target, sf::Vector2i mousePosition) const;

    void HandleKeyPressed(sf::Keyboard::Scancode key);
    void HandleMouseButtonPressed(sf::Mouse::Button button, sf::Vector2i position);
    void HandleMouseButtonReleased(sf::Mouse::Button button, sf::Vector2i position);
    void HandleMouseMoved(sf::Vector2i position);
    void HandleMouseLeftWindow();
};