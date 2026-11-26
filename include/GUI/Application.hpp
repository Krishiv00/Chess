#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "SFML/Graphics.hpp"

#include "Engine/Chess.hpp"

#include "GUI/Game/EngineRunner.hpp"
#include "GUI/Game/Game.hpp"

#include "GUI/BoardView.hpp"
#include "GUI/Settings.hpp"
#include "GUI/SoundSystem.hpp"
#include "GUI/Themes.hpp"
#include "GUI/UI/UI.hpp"
#include "GUI/UI/Widgets.hpp"

namespace Panel {
    class PlayerCard;
    class MoveList;
}

class PieceSets final {
public:
    struct Entry {
        std::string Id;
        std::string Name;
        std::filesystem::path Path;
    };

private:
    std::vector<Entry> m_Entries;
    mutable std::vector<std::optional<sf::Texture>> m_Previews;
    mutable std::vector<bool> m_PreviewTried;

public:
    void Scan(const std::filesystem::path& directory);

    [[nodiscard]]
    inline std::size_t Count() const noexcept {
        return m_Entries.size();
    }

    [[nodiscard]]
    inline const Entry& At(std::size_t index) const {
        return m_Entries[index];
    }

    [[nodiscard]]
    std::size_t IndexOf(const std::string& id) const;

    [[nodiscard]]
    const sf::Texture* Preview(std::size_t index) const;
};

class Application final {
private:
    [[nodiscard]]
    Chess::PieceColor engineColor() const noexcept;

    [[nodiscard]]
    Chess::PieceColor humanSide() const noexcept;

    [[nodiscard]]
    bool humanCanMove() const noexcept;

    // Game flow
    bool startNewGame(GameMode mode, bool playAsBlack, const std::string& fen, bool playSound = true);
    void applyMove(Chess::Move move, bool animate);
    void afterPositionChanged();
    void startSearch(bool suggestion);
    void playBestMove();
    void toggleInspection();
    void flipSideToMove();
    void offerDraw();
    void resign();
    void concludeIfOver();
    void endGame();
    void updateEvaluation();
    void copyFen();
    void loadFenFromClipboard();
    void notify(std::string text);

    // Settings
    void applyEngineConfig();
    void scheduleEngineConfig();
    void applySound();
    void setTheme(std::size_t index);
    bool setPieceSet(std::size_t index);
    void saveSettings() const;

    // UI
    void buildPanel();
    void syncPanel();
    void layout();
    void reapDialogs();
    void showDialog(std::unique_ptr<UI::Dialog> dialog);
    void closeDialog();
    void openMenu();
    void openNewGame();
    void openConfirm(std::string title, std::string text, std::string confirmLabel, std::function<void()> onConfirm);

    Game m_Game;
    EngineRunner m_Engine{m_Game.Board()};

    GameMode m_Mode{GameMode::Local};
    Chess::PieceColor m_PlayerColor{Chess::PieceColor::White};
    Chess::PieceColor m_LastEngineColor{Chess::PieceColor::White};

    Settings m_Settings;
    std::filesystem::path m_SettingsPath;
    bool m_ConfigDirty{false};
    float m_ConfigTimer{0.f};

    // Resources
    sf::Font m_Font;
    sf::Texture m_IconTexture;
    sf::Texture m_PieceTexture;
    PieceSets m_PieceSets;
    SfxPlayer m_Sfx;

    // Look
    std::size_t m_ThemeIdx{0};
    std::size_t m_PieceIdx{0};
    std::unique_ptr<Themes::Theme> m_Theme;
    UI::Context m_Ctx;

    // Layout and widgets
    sf::Vector2u m_WindowSize;
    sf::FloatRect m_EvalBarRect;
    sf::FloatRect m_PanelRect;

    BoardView m_BoardView{m_Game, m_PieceTexture};
    EvaluationBar m_EvalBar;

    std::unique_ptr<UI::Column> m_Panel;
    Panel::PlayerCard* m_TopCard{nullptr};
    Panel::PlayerCard* m_BottomCard{nullptr};
    Panel::MoveList* m_MoveList{nullptr};
    UI::Label* m_BannerText{nullptr};
    UI::Button* m_HintButton{nullptr};
    UI::Button* m_PlayButton{nullptr};
    UI::Button* m_InspectButton{nullptr};
    UI::Button* m_DrawButton{nullptr};
    UI::Button* m_ResignButton{nullptr};

    std::unique_ptr<UI::Dialog> m_Dialog;
    std::unique_ptr<UI::Dialog> m_NextDialog;

    std::string m_Message;
    float m_MessageTimer{0.f};

    float m_DrawOfferTimer{0.f};

    // what the new game dialog shows, lives as long as the app does
    GameMode m_NewGameMode{GameMode::Local};
    bool m_NewGameBlack{false};

public:
    Application();
    ~Application();

    void SetTargetSize(sf::Vector2u size);

    void SetWindowState(sf::Vector2u restoredSize, bool maximized);

    [[nodiscard]]
    bool LoadResources(const std::filesystem::path& root);

    void Update(float deltaTime, sf::Vector2i mousePosition);
    void Render(sf::RenderTarget& target, sf::Vector2i mousePosition) const;

    [[nodiscard]]
    bool HandleKeyPressed(sf::Keyboard::Scancode key, bool control);

    void HandleTextEntered(char32_t unicode);
    void HandleMouseButtonPressed(sf::Mouse::Button button, sf::Vector2i position);
    void HandleMouseButtonReleased(sf::Mouse::Button button, sf::Vector2i position);
    void HandleMouseMoved(sf::Vector2i position);
    void HandleMouseWheel(sf::Vector2i position, float delta);
};
