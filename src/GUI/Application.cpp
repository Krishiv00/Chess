#include "GUI/Application.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iostream>

#include "GUI/Game/Game.hpp"

#pragma region Piece sets

namespace {
    // unanswered draw offers lapse after this long
    constexpr float DrawOfferDuration = 10.f;

    // "neo-wood" -> "Neo Wood"
    std::string DisplayName(const std::string& id) {
        std::string name = id;
        bool wordStart = true;

        for (char& c : name) {
            if (c == '-' || c == '_') c = ' ';

            if (wordStart && std::isalpha(static_cast<unsigned char>(c))) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            wordStart = c == ' ';
        }

        return name;
    }

    const char* ColorName(Chess::PieceColor color) {
        return color == Chess::PieceColor::White ? "White" : "Black";
    }

    std::pair<std::string, std::string> DescribeResult(const Game& game) {
        const std::string winner = game.Winner() ? ColorName(*game.Winner()) : "";
        const std::string loser = game.Winner() ? ColorName(Chess::InvertColor(*game.Winner())) : "";

        switch (game.Result()) {
        case GameResult::Checkmate: return {"CHECKMATE", winner + " wins."};
        case GameResult::Stalemate: return {"STALEMATE", "No legal moves - it's a draw."};
        case GameResult::DrawFiftyMove: return {"DRAW", "50-move rule."};
        case GameResult::DrawRepetition: return {"DRAW", "Threefold repetition."};
        case GameResult::DrawInsufficient: return {"DRAW", "Insufficient material."};
        case GameResult::DrawAgreement: return {"DRAW", "Agreed by both players."};
        case GameResult::Resignation: return {"RESIGNED", loser + " resigned. " + winner + " wins."};
        case GameResult::Abandoned: return {"OPPONENT LEFT", loser + " disconnected. " + winner + " wins."};
        default: return {};
        }
    }
}

void PieceSets::Scan(const std::filesystem::path& directory) {
    m_Entries.clear();

    std::error_code error;

    for (const auto& item : std::filesystem::directory_iterator(directory, error)) {
        std::string extension = item.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });

        if (!item.is_regular_file() || extension != ".png") continue;

        const std::string id = item.path().stem().string();
        m_Entries.push_back({id, DisplayName(id), item.path()});
    }

    std::sort(m_Entries.begin(), m_Entries.end(), [](const Entry& a, const Entry& b) { return a.Id < b.Id; });

    m_Previews.assign(m_Entries.size(), std::nullopt);
    m_PreviewTried.assign(m_Entries.size(), false);
}

std::size_t PieceSets::IndexOf(const std::string& id) const {
    for (std::size_t i = 0; i < m_Entries.size(); ++i) {
        if (m_Entries[i].Id == id) return i;
    }

    return 0;
}

const sf::Texture* PieceSets::Preview(std::size_t index) const {
    if (index >= m_Entries.size()) return nullptr;

    if (!m_PreviewTried[index]) {
        m_PreviewTried[index] = true;

        sf::Image image;

        if (image.loadFromFile(m_Entries[index].Path)) {
            const unsigned int cellWidth = image.getSize().x / 6;
            const unsigned int cellHeight = image.getSize().y / 2;

            // the knights sit in the third column, white on top
            constexpr unsigned int KnightColumn = 2;

            sf::Image preview(sf::Vector2u(cellWidth * 2, cellHeight), sf::Color::Transparent);

            (void)preview.copy(image, {0, 0}, sf::IntRect({static_cast<int>(cellWidth * KnightColumn), 0}, {static_cast<int>(cellWidth), static_cast<int>(cellHeight)}));
            (void)preview.copy(image, {cellWidth, 0}, sf::IntRect({static_cast<int>(cellWidth * KnightColumn), static_cast<int>(cellHeight)}, {static_cast<int>(cellWidth), static_cast<int>(cellHeight)}));

            sf::Texture texture;

            if (texture.loadFromImage(preview)) {
                texture.setSmooth(true);
                (void)texture.generateMipmap();

                m_Previews[index] = std::move(texture);
            }
        }
    }

    return m_Previews[index] ? &*m_Previews[index] : nullptr;
}

#pragma region Side panel widgets

namespace Panel {
    class PlayerCard final : public UI::Widget {
    public:
        struct Data {
            std::string Name;
            std::string Subtitle;
            std::string Avatar;
            sf::Color AvatarFill;
            sf::Color AvatarInk;
            bool Turn{false};
        };

    private:
        Data m_Data;

    public:
        explicit PlayerCard(const UI::Context& context) : Widget(context) {}

        void Set(Data data) {
            m_Data = std::move(data);
        }

        float PreferredHeight(float) const override {
            return m_Ctx.Px(56.f);
        }

        void Render(const UI::Painter& painter) const override {
            const UI::Palette& pal = m_Ctx.Pal;
            const float x = m_Rect.position.x, y = m_Rect.position.y, h = m_Rect.size.y;

            const sf::FloatRect avatar({x, y + (h - m_Ctx.Px(44.f)) * 0.5f}, {m_Ctx.Px(44.f), m_Ctx.Px(44.f)});

            painter.Round(avatar, m_Data.AvatarFill, m_Ctx.Px(6.f));

            // the side to move gets an accent ring
            if (m_Data.Turn) painter.Ring(avatar, pal.Accent, m_Ctx.Px(6.f), m_Ctx.Px(2.f));

            painter.Text(m_Data.Avatar, avatar, m_Ctx.FontPx(13.f), m_Data.AvatarInk, UI::Align::Center, true);

            const float textX = x + m_Ctx.Px(56.f);
            const float clockWidth = m_Ctx.Px(84.f);
            const float textWidth = m_Rect.size.x - m_Ctx.Px(56.f) - clockWidth - m_Ctx.Px(8.f);

            painter.Text(m_Data.Name, sf::FloatRect({textX, y + h * 0.1f}, {textWidth, h * 0.42f}), m_Ctx.FontPx(15.f), pal.Text, UI::Align::Left, true);

            painter.Text(
                m_Ctx.Fit(m_Data.Subtitle, m_Ctx.FontPx(12.f), textWidth), sf::FloatRect({textX, y + h * 0.52f}, {textWidth, h * 0.36f}),
                m_Ctx.FontPx(12.f), pal.TextDim
            );

            // no clocks yet, the slot keeps the layout ready for them
            const sf::FloatRect clock({x + m_Rect.size.x - clockWidth, y + (h - m_Ctx.Px(36.f)) * 0.5f}, {clockWidth, m_Ctx.Px(36.f)});

            painter.Round(clock, pal.Inset, m_Ctx.Px(6.f));
            painter.Text("--:--", clock, m_Ctx.FontPx(17.f), pal.TextDim, UI::Align::Center, true);
        }
    };

    class MoveList final : public UI::Widget {
    private:
        class Rows final : public UI::Widget {
        private:
            const Game& m_Game;

            static constexpr float RowHeight = 28.f;
            static constexpr float NumberWidth = 34.f;

            [[nodiscard]]
            int rowCount() const {
                const int plies = static_cast<int>(m_Game.History().size()) + (m_Game.FirstSide() == Chess::PieceColor::Black ? 1 : 0);
                return (plies + 1) / 2;
            }

        public:
            Rows(const UI::Context& context, const Game& game) : Widget(context), m_Game(game) {}

            float PreferredHeight(float) const override {
                return static_cast<float>(rowCount()) * m_Ctx.Px(RowHeight);
            }

            void Render(const UI::Painter& painter) const override {
                const UI::Palette& pal = m_Ctx.Pal;
                const auto& history = m_Game.History();

                const int offset = m_Game.FirstSide() == Chess::PieceColor::Black ? 1 : 0;
                const float rowHeight = m_Ctx.Px(RowHeight);
                const float numberWidth = m_Ctx.Px(NumberWidth);
                const float cellWidth = (m_Rect.size.x - numberWidth) * 0.5f;
                const unsigned int size = m_Ctx.FontPx(14.f);

                // only the rows inside the visible area are touched, however long the game gets
                const sf::FloatRect& visible = painter.VisibleRect();
                const int rows = rowCount();
                const int firstRow = std::max(0, static_cast<int>(std::floor((visible.position.y - m_Rect.position.y) / rowHeight)));
                const int lastRow = std::min(rows - 1, static_cast<int>(std::floor((visible.position.y + visible.size.y - m_Rect.position.y) / rowHeight)));

                for (int row = firstRow; row <= lastRow; ++row) {
                    const float y = m_Rect.position.y + static_cast<float>(row) * rowHeight;

                    painter.Text(
                        std::to_string(m_Game.FirstMoveNumber() + static_cast<unsigned int>(row)) + ".",
                        sf::FloatRect({m_Rect.position.x + m_Ctx.Px(4.f), y}, {numberWidth, rowHeight}), size, pal.TextDim
                    );

                    for (int column = 0; column < 2; ++column) {
                        const int ply = row * 2 + column - offset;
                        const sf::FloatRect cell(
                            {m_Rect.position.x + numberWidth + static_cast<float>(column) * cellWidth, y + m_Ctx.Px(2.f)},
                            {cellWidth - m_Ctx.Px(4.f), rowHeight - m_Ctx.Px(4.f)}
                        );

                        if (ply < 0) {
                            painter.Text("...", cell, size, pal.TextDim, UI::Align::Left);
                            continue;
                        }

                        if (ply >= static_cast<int>(history.size())) continue;

                        const bool latest = ply == static_cast<int>(history.size()) - 1;

                        if (latest) painter.Round(cell, pal.Raised, m_Ctx.Px(4.f));

                        painter.Text(
                            history[static_cast<std::size_t>(ply)].San,
                            sf::FloatRect({cell.position.x + m_Ctx.Px(8.f), cell.position.y}, {cell.size.x - m_Ctx.Px(8.f), cell.size.y}),
                            size, pal.Text, UI::Align::Left, latest
                        );
                    }
                }
            }
        };

        const Game& m_Game;
        UI::ScrollView m_Scroll;
        std::size_t m_Count{0};

    public:
        MoveList(const UI::Context& context, const Game& game) :
            Widget(context), m_Game(game), m_Scroll(context, std::make_unique<Rows>(context, game)) {}

        float PreferredHeight(float) const override {
            return 0.f;
        }

        void SetRect(sf::FloatRect rect) override {
            Widget::SetRect(rect);

            const float pad = m_Ctx.Px(8.f), title = m_Ctx.Px(28.f);
            m_Scroll.SetRect(sf::FloatRect({rect.position.x + pad, rect.position.y + title}, {rect.size.x - 2.f * pad, rect.size.y - title - pad}));
        }

        void Update(float deltaTime, sf::Vector2f mouse) override {
            if (m_Game.History().size() != m_Count) {
                m_Count = m_Game.History().size();
                m_Scroll.ScrollToEnd();
            }

            m_Scroll.Update(deltaTime, mouse);
        }

        void Render(const UI::Painter& painter) const override {
            painter.Round(m_Rect, m_Ctx.Pal.Inset, m_Ctx.Px(8.f));

            painter.Text(
                "MOVES", sf::FloatRect({m_Rect.position.x + m_Ctx.Px(14.f), m_Rect.position.y}, {m_Rect.size.x, m_Ctx.Px(28.f)}),
                m_Ctx.FontPx(11.5f), m_Ctx.Pal.TextDim, UI::Align::Left, true
            );

            m_Scroll.Render(painter);
        }

        bool OnScroll(sf::Vector2f position, float delta) override {
            return m_Scroll.OnScroll(position, delta);
        }
    };
}

#pragma region Setup

Application::Application() {
    Chess::Init();
}

Application::~Application() {
    saveSettings();
}

void Application::saveSettings() const {
    if (!m_SettingsPath.empty()) m_Settings.Save(m_SettingsPath);
}

bool Application::LoadResources(const std::filesystem::path& root) {
    const std::filesystem::path texturePath = root / "Textures";

    m_SettingsPath = "Settings.cfg";
    m_Settings.Load(m_SettingsPath);

    if (!m_Font.openFromFile(root / "Fonts" / "Font.ttf")) [[unlikely]] {
        std::cerr << "Failed to open font" << std::endl;
        return false;
    }

    m_Font.setSmooth(false);

    if (!m_IconTexture.loadFromFile(texturePath / "Icons.png")) [[unlikely]] {
        std::cerr << "Failed to load icon texture" << std::endl;
        return false;
    }

    m_IconTexture.setSmooth(true);
    (void)m_IconTexture.generateMipmap();

    m_PieceSets.Scan(texturePath / "Pieces");

    // the saved set first, then whatever else loads
    bool piecesLoaded = false;
    const std::size_t preferred = m_PieceSets.IndexOf(m_Settings.PieceSet);

    for (std::size_t i = 0; i < m_PieceSets.Count() && !piecesLoaded; ++i) {
        piecesLoaded = setPieceSet((preferred + i) % m_PieceSets.Count());
    }

    if (!piecesLoaded) [[unlikely]] {
        std::cerr << "No piece textures found in " << (texturePath / "Pieces") << std::endl;
        return false;
    }

    const std::filesystem::path soundPath = root / "Sounds";

    constexpr std::array<std::pair<Sfx, const char*>, 10> Sounds = {{
        {Sfx::Capture, "Capture.wav"}, {Sfx::Check, "Check.wav"}, {Sfx::GameEnd, "Game End.wav"},
        {Sfx::MoveSelf, "Move Self.wav"}, {Sfx::MoveOpponent, "Move Opp.wav"}, {Sfx::Promotion, "Promotion.wav"},
        {Sfx::Notify, "Notify.wav"}, {Sfx::Illegal, "Illegal.wav"}, {Sfx::SpecialMove, "Special Move.wav"}, {Sfx::GameStart, "Game Start.wav"}
        }};

    for (const auto& [sfx, file] : Sounds) {
        if (!m_Sfx.LoadFromFile(sfx, soundPath / file)) [[unlikely]] {
            std::cerr << "Failed to load sound " << file << std::endl;
            return false;
        }
    }

    m_Ctx.Font = &m_Font;
    m_Ctx.Icons = &m_IconTexture;

    setTheme(Themes::IndexOf(m_Settings.Theme));
    applySound();

    m_BoardView.OnMove = [this](Chess::Move move, bool animate) {
        applyMove(move, animate);

        // moves made while inspecting are only for looking around
        if (online() && !m_Game.Inspecting()) m_Net.Send(Net::EventType::Move, Chess::MoveToUCI(move));
        };
    m_BoardView.OnIllegalMove = [this]() { m_Sfx.Play(Sfx::Illegal); };
    m_BoardView.OnPromotionPrompt = [this]() { m_Sfx.Play(Sfx::Notify); };

    buildPanel();
    layout();

    applyEngineConfig();
    // every launch starts a local game, other modes are started on purpose
    (void)startNewGame(GameMode::Local, false, "", false);

    return true;
}

void Application::SetTargetSize(sf::Vector2u size) {
    m_WindowSize = size;
    layout();
}

void Application::SetWindowState(sf::Vector2u restoredSize, bool maximized) {
    m_Settings.WindowWidth = static_cast<int>(restoredSize.x);
    m_Settings.WindowHeight = static_cast<int>(restoredSize.y);
    m_Settings.WindowMaximized = maximized;

    saveSettings();
}

void Application::layout() {
    const float width = static_cast<float>(m_WindowSize.x);
    const float height = static_cast<float>(m_WindowSize.y);

    m_Ctx.Scale = height / 800.f;

    const float evalWidth = evalBarVisible() ? height * 0.03f : 0.f;

    m_EvalBarRect = sf::FloatRect({0.f, 0.f}, {evalWidth, height});
    m_BoardView.SetGeometry({evalWidth, 0.f}, height / Chess::Ranks);

    // the panel takes whatever the board leaves, so hiding the eval bar widens it
    m_PanelRect = sf::FloatRect({evalWidth + height, 0.f}, {std::max(0.f, width - evalWidth - height), height});

    if (m_Panel) m_Panel->SetRect(m_PanelRect);
    if (m_Dialog) m_Dialog->SetWindow(sf::Vector2f(m_WindowSize));
}

void Application::setTheme(std::size_t index) {
    m_ThemeIdx = std::min(index, Themes::Count - 1);
    m_Theme = Themes::Create(m_ThemeIdx);

    m_Settings.Theme = std::string(m_Theme->GetName());
    m_Ctx.Pal = UI::Palette::FromAccent(m_Theme->GetCheckerboard()[1]);

    m_BoardView.SetTheme(*m_Theme);
}

bool Application::setPieceSet(std::size_t index) {
    if (index >= m_PieceSets.Count()) return false;

    sf::Texture texture;

    if (!texture.loadFromFile(m_PieceSets.At(index).Path)) {
        notify("Couldn't load " + m_PieceSets.At(index).Name);
        return false;
    }

    m_PieceTexture = std::move(texture);
    m_PieceIdx = index;
    m_Settings.PieceSet = m_PieceSets.At(index).Id;

    return true;
}

void Application::applySound() {
    m_Sfx.SetEnabled(m_Settings.SoundEnabled);
    m_Sfx.SetVolume(static_cast<float>(m_Settings.Volume));
}

void Application::applyEngineConfig() {
    m_Engine.Reset();

    const Chess::Board& board = m_Game.Board();

    board.SetHashSize(static_cast<unsigned int>(m_Settings.HashMB));
    board.SetCatchAll(m_Settings.CatchAll);
    board.SetThreads(static_cast<unsigned int>(m_Settings.Threads));

    m_ConfigDirty = false;
}

// Hash and thread changes rebuild engine tables, so holding a stepper waits until it settles
void Application::scheduleEngineConfig() {
    m_ConfigDirty = true;
    m_ConfigTimer = 0.4f;
}

#pragma region Game flow

Chess::PieceColor Application::engineColor() const noexcept {
    return Chess::InvertColor(m_PlayerColor);
}

Chess::PieceColor Application::humanSide() const noexcept {
    return m_Mode == GameMode::Local ? m_Game.SideToMove() : m_PlayerColor;
}

bool Application::humanCanMove() const noexcept {
    if (m_Game.Over() || m_Engine.Thinking() || m_Dialog) return false;

    if (online()) return m_Game.Inspecting() || (m_Net.Connected() && m_Game.SideToMove() == m_PlayerColor);

    return m_Game.Inspecting() || m_Mode != GameMode::Engine || m_Game.SideToMove() == m_PlayerColor;
}

bool Application::evalBarVisible() const noexcept {
    return m_Settings.ShowEvalBar && (!online() || m_NetEval);
}

bool Application::startNewGame(GameMode mode, bool playAsBlack, const std::string& fen, bool playSound) {
    const std::string position = fen.empty() ? std::string(Chess::DefaultFEN) : fen;

    if (!Fen::Parse(position)) return false;

    if (mode != GameMode::Online) m_Net.Close();

    m_Engine.Reset();
    (void)m_Game.Reset(position);

    m_Rematch = Rematch::None;
    m_Mode = mode;
    m_PlayerColor = playAsBlack ? Chess::PieceColor::Black : Chess::PieceColor::White;

    m_BoardView.NewGame();
    m_BoardView.SetFlipped(mode != GameMode::Local && playAsBlack);
    layout();

    if (playSound) m_Sfx.Play(Sfx::GameStart);

    updateEvaluation();
    concludeIfOver();
    afterPositionChanged();

    return true;
}

void Application::applyMove(Chess::Move move, bool animate) {
    m_Engine.StopPonder();

    const Chess::PieceColor mover = m_Game.SideToMove();

    m_Game.DoMove(move);
    m_BoardView.AnimateMove(move, animate);

    concludeIfOver();

    const bool selfMove = (mover == Chess::PieceColor::White) != m_BoardView.Flipped();

    if (m_Game.Board().isUnderCheck(m_Game.SideToMove())) m_Sfx.Play(Sfx::Check);
    else if (Chess::isCastle(move.Flag)) m_Sfx.Play(Sfx::SpecialMove);
    else if (Chess::isPromotion(move.Flag)) m_Sfx.Play(Sfx::Promotion);
    else if (Chess::HasFlag(move.Flag, Chess::MoveFlag::Capture) || Chess::HasFlag(move.Flag, Chess::MoveFlag::EnPassant)) m_Sfx.Play(Sfx::Capture);
    else m_Sfx.Play(selfMove ? Sfx::MoveSelf : Sfx::MoveOpponent);

    updateEvaluation();
    afterPositionChanged();
}

// Decides what the engine does next, called whenever the position or the rules around it changed
void Application::afterPositionChanged() {
    if (m_Game.Over() || m_Game.Inspecting() || m_Mode != GameMode::Engine) return;

    if (m_Game.SideToMove() == engineColor()) {
        startSearch(false);
    } else if (m_Settings.Ponder && !m_Engine.Thinking()) {
        m_Engine.Ponder(m_Game.Board(), m_PlayerColor);
    }
}

void Application::startSearch(bool suggestion) {
    if (m_Game.Over() || m_Engine.Thinking() || online()) return;

    // the hash is about to be touched
    m_Engine.StopPonder();

    const Chess::Board& board = m_Game.Board();
    const Chess::PieceColor side = m_Game.SideToMove();

    board.SetEngineColor(side);

    if (m_Settings.CatchAll && side != m_LastEngineColor) board.ClearHash();

    m_LastEngineColor = side;

    SearchLimit limit;
    limit.ByDepth = m_Settings.LimitByDepth;
    limit.TimeMs = m_Settings.ThinkTimeMs;
    limit.Depth = m_Settings.SearchDepth;
    limit.UseBook = m_Settings.OwnBook;

    m_Engine.Search(board, side, limit, suggestion);
}

void Application::playBestMove() {
    if (m_Engine.Thinking()) m_Engine.Skip();
    else startSearch(false);
}

void Application::toggleInspection() {
    if (m_Engine.Thinking()) return;

    m_Engine.Reset();

    if (m_Game.Inspecting()) {
        m_Game.EndInspection();
        m_BoardView.NewGame();

        updateEvaluation();
        afterPositionChanged();
    } else if (!m_Game.Over()) {
        m_Game.BeginInspection();
    }
}

void Application::flipSideToMove() {
    if (online()) {
        notify("Not available in online games");
        return;
    }

    m_Engine.Reset();
    m_Game.FlipSideToMove();
    m_BoardView.NewGame();

    updateEvaluation();
    concludeIfOver();
    afterPositionChanged();
}

void Application::offerDraw() {
    if (m_Game.Over() || m_Game.Inspecting()) return;

    if (m_Mode != GameMode::Engine) {
        m_Game.OfferDraw(humanSide());

        // small buffer to counter the lag
        m_DrawOfferTimer = DrawOfferDuration + (online() ? 2.f : 0.f);

        if (online()) m_Net.Send(Net::EventType::DrawOffer);
        return;
    }

    // the engine only takes a draw when the position is about equal, the evaluation is white's share
    // of the position so 0.5 is level and 0.05 is roughly a fifth of a pawn either way
    constexpr float DrawMargin = 0.05f;

    if (std::abs(m_EvalBar.GetEval() - 0.5f) <= DrawMargin) {
        m_Game.AcceptDraw();
        endGame();
    } else {
        notify(std::string(Chess::EngineName) + " declined the draw");
    }
}

void Application::resign() {
    const std::string text = m_Mode == GameMode::Local ? std::string(ColorName(humanSide())) + " will lose this game." : "You will lose this game.";

    openConfirm("Resign?", text, "Resign", [this]() {
        if (m_Game.Over()) return;

        m_Game.Resign(humanSide());

        if (online()) m_Net.Send(Net::EventType::Resign);

        endGame();
    });
}

void Application::concludeIfOver() {
    if (!m_Game.Over()) return;

    m_Sfx.Play(Sfx::GameEnd);

    if (m_Game.Inspecting()) return;

    // confetti for a win in a game with a side of mine, and for either side's win when both are played here
    const std::optional<Chess::PieceColor> winner = m_Game.Winner();
    const bool celebrate = winner && (m_Mode == GameMode::Local || *winner == m_PlayerColor);

    const auto [title, subtitle] = DescribeResult(m_Game);
    m_BoardView.ShowGameOver(title, subtitle, celebrate);
}

// Wraps up a game that ended without a move, resigning or an accepted draw
void Application::endGame() {
    m_Engine.Reset();
    updateEvaluation();
    concludeIfOver();
}

void Application::updateEvaluation() {
    if (m_Game.Over()) {
        const std::optional<Chess::PieceColor> winner = m_Game.Winner();

        m_EvalBar.SetEval(!winner ? 0.5f : *winner == Chess::PieceColor::White ? 1.f : 0.f);
        return;
    }

    m_EvalBar.SetEval(m_Game.Board().GetConfidence(m_Game.SideToMove(), m_Settings.EvalDepth));
}

void Application::copyFen() {
    sf::Clipboard::setString(m_Game.Board().GetFen(m_Game.SideToMove()));
    notify("FEN copied");
}

void Application::notify(std::string text, float seconds) {
    m_Message = std::move(text);
    m_MessageTimer = seconds;
}

#pragma region Online

Net::Session::Config Application::netConfig() const {
    return {m_Settings.PlayerName.empty() ? "Player" : m_Settings.PlayerName, Net::RelayUrl};
}

void Application::startHosting(bool playAsBlack, const std::string& fen, bool evalBar) {
    const std::optional<Fen::Parsed> parsed = Fen::Parse(fen.empty() ? std::string(Chess::DefaultFEN) : fen);

    m_NetHost = true;
    m_NetHostBlack = playAsBlack;
    m_NetEval = evalBar;
    m_NetBegun = false;
    m_NetFen = parsed ? parsed->Text : std::string(Chess::DefaultFEN);

    m_Net.Host(Net::GenerateCode(), netConfig());

    openOnlineWait();
}

void Application::startJoining(const std::string& code) {
    m_NetHost = false;
    m_NetBegun = false;
    m_JoinCode = code;

    m_Net.Join(code, netConfig());

    openOnlineWait();
}

bool Application::beginOnlineGame(bool playAsBlack, const std::string& fen) {
    m_NetBegun = true;

    if (m_WaitDialog && m_Dialog.get() == m_WaitDialog) closeDialog();

    const std::string unplayable = "That starting position can't be played, the online game was cancelled.";

    if (!Fen::Plausible(fen)) {
        dropOnline(unplayable);
        return false;
    }

    (void)startNewGame(GameMode::Online, playAsBlack, fen);

    if (m_Game.Board().isUnderCheck(Chess::InvertColor(m_Game.SideToMove()))) {
        (void)startNewGame(GameMode::Local, false, "", false);
        notify(unplayable, 10.f);
        return false;
    }

    m_Net.SetResumable(true);

    return true;
}

void Application::leaveInspection() {
    if (!m_Game.Inspecting()) return;

    m_Game.EndInspection();
    m_BoardView.NewGame();

    updateEvaluation();
}

void Application::offerRematch() {
    if (!online() || !m_Net.Connected() || !m_Game.Over() || m_Rematch != Rematch::None) return;

    m_Rematch = Rematch::Sent;

    // small buffer like draw offer
    m_RematchTimer = DrawOfferDuration + 2.f;

    m_Net.Send(Net::EventType::RematchOffer);
}

void Application::answerRematch(bool accept) {
    if (m_Rematch != Rematch::Received) return;

    m_Net.Send(accept ? Net::EventType::RematchAccept : Net::EventType::RematchDecline);
    m_Rematch = Rematch::None;

    if (accept) restartOnline();
    else notify("Rematch declined");
}

void Application::restartOnline() {
    if (!m_NetHost) return;

    m_NetHostBlack = !m_NetHostBlack;

    if (beginOnlineGame(m_NetHostBlack, m_NetFen)) m_Net.Send(Net::EventType::Start, m_NetFen, !m_NetHostBlack, m_NetEval);
}

void Application::pumpNetwork(float deltaTime) {
    m_Net.Update(deltaTime);

    while (const std::optional<Net::Event> event = m_Net.Poll()) handleNetEvent(*event);

    if (m_WaitDialog && m_WaitStatus) {
        const bool failed = m_Net.GetState() == Net::Session::State::Closed;

        if (m_WaitCode) m_WaitCode->SetText(Net::FormatCode(m_Net.Code()));

        m_WaitStatus->SetText(m_Net.Status());
        m_WaitStatus->SetTone(failed ? UI::Tone::Danger : UI::Tone::Normal);
        m_WaitInfo->SetText(m_Net.RelayInfo());
    }
}

void Application::handleNetEvent(const Net::Event& event) {
    if (m_NetBegun && online() && (event.Type == Net::EventType::Move || event.Type == Net::EventType::Resign ||
        event.Type == Net::EventType::DrawOffer || event.Type == Net::EventType::DrawAccept ||
        event.Type == Net::EventType::Resumed || event.Type == Net::EventType::Sync)) {
        leaveInspection();
    }

    switch (event.Type) {
    case Net::EventType::Connected:
        if (m_NetHost && beginOnlineGame(m_NetHostBlack, m_NetFen)) m_Net.Send(Net::EventType::Start, m_NetFen, !m_NetHostBlack, m_NetEval);
        break;

    case Net::EventType::Start:
        if (!m_NetHost && (!m_NetBegun || m_Game.Over())) {
            m_NetEval = event.Option;
            (void)beginOnlineGame(event.Flag, event.Text);
        }
        break;

    case Net::EventType::Closed:
        m_Rematch = Rematch::None;

        if (m_NetBegun && online() && !m_Game.Over()) forfeitOpponent(event.Text);
        break;

    case Net::EventType::Reconnecting:
        break;

    case Net::EventType::Resumed:
        if (!m_NetBegun || !online()) break;

        notify("Reconnected");

        m_Net.Send(Net::EventType::Sync, std::to_string(m_Game.History().size()));
        break;

    case Net::EventType::Sync: {
        if (!m_NetBegun || !online()) break;

        const std::size_t mine = m_Game.History().size();
        std::size_t theirs = 0;
        (void)std::from_chars(event.Text.data(), event.Text.data() + event.Text.size(), theirs);

        if (mine == theirs + 1) m_Net.Send(Net::EventType::Move, Chess::MoveToUCI(m_Game.History().back().Move));
        else if (mine != theirs && mine + 1 != theirs) dropOnline("The two games got out of sync, the online game was ended.");
        break;
    }

    case Net::EventType::RematchOffer:
        if (!m_NetBegun || !online() || !m_Game.Over()) break;

        if (m_Rematch == Rematch::Sent) {
            // both asked at the same moment
            m_Rematch = Rematch::None;
            restartOnline();
        } else if (m_Rematch == Rematch::None) {
            m_Rematch = Rematch::Received;
            m_RematchTimer = DrawOfferDuration;
        }
        break;

    case Net::EventType::RematchAccept:
        if (m_Rematch == Rematch::Sent) {
            m_Rematch = Rematch::None;
            restartOnline();
        }
        break;

    case Net::EventType::RematchDecline:
        if (m_Rematch == Rematch::Sent) {
            m_Rematch = Rematch::None;
            notify("Your opponent declined the rematch");
        }
        break;

    case Net::EventType::Move:
        if (m_NetBegun && online()) receiveMove(event.Text);
        break;

    case Net::EventType::Resign:
        if (m_NetBegun && online() && !m_Game.Over()) {
            m_Game.Resign(Chess::InvertColor(m_PlayerColor));
            endGame();
        }
        break;

    case Net::EventType::DrawOffer:
        if (m_NetBegun && online() && !m_Game.Over()) {
            m_Game.OfferDraw(Chess::InvertColor(m_PlayerColor));
            m_DrawOfferTimer = DrawOfferDuration;
        }
        break;

    case Net::EventType::DrawAccept:
        if (m_NetBegun && online() && m_Game.DrawOfferedBy() == m_PlayerColor) {
            m_Game.AcceptDraw();
            endGame();
        }
        break;

    case Net::EventType::DrawDecline:
        if (m_NetBegun && online() && m_Game.DrawOfferedBy() == m_PlayerColor) {
            m_Game.DeclineDraw();
            notify("Your opponent declined the draw");
        }
        break;
    }
}

void Application::receiveMove(const std::string& uci) {
    if (m_Game.Over()) return;

    const Chess::PieceColor mover = m_Game.SideToMove();

    if (mover == m_PlayerColor) {
        kickOpponent("Your opponent moved out of turn.");
        return;
    }

    const Chess::Board& board = m_Game.Board();
    const uint64_t pieces = board.GetOccupancyMap(mover);

    for (int square = 0; square < Chess::NullPos; ++square) {
        if (!(pieces & Chess::IndexToMask(square))) continue;

        for (const Chess::Move& move : board.GetLegalMoves(square)) {
            if (Chess::MoveToUCI(move) != uci) continue;

            applyMove(move, true);
            return;
        }
    }

    kickOpponent("Your opponent sent an illegal move.");
}

void Application::forfeitOpponent(const std::string& reason) {
    leaveInspection();

    if (!m_Game.Over()) {
        m_Game.Abandon(Chess::InvertColor(m_PlayerColor));
        endGame();
    }

    notify(reason, 10.f);
}

void Application::kickOpponent(const std::string& reason) {
    m_Net.Close();
    forfeitOpponent(reason);
}

void Application::dropOnline(const std::string& reason) {
    m_Net.Close();

    if (m_WaitDialog && m_Dialog.get() == m_WaitDialog) closeDialog();

    notify(reason, 10.f);
}

#pragma region Side panel

void Application::buildPanel() {
    using namespace UI;

    m_Panel = std::make_unique<Column>(m_Ctx, 12.f, 16.f, 16.f);

    m_TopCard = &m_Panel->Add<Panel::PlayerCard>();

    m_MoveList = &m_Panel->AddFlex<Panel::MoveList>(m_Game);
    m_MoveList->ShowIf([this]() { return m_Settings.ShowMoveList; });

    m_Panel->AddFlex<Spacer>().ShowIf([this]() { return !m_Settings.ShowMoveList; });

    // shows a draw offer that needs an answer, or a short message such as a declined draw
    Column& banner = m_Panel->Add<Column>(10.f, 12.f, 12.f);
    banner.Background(m_Ctx.Pal.WarningBackground, m_Ctx.Pal.Warning, 8.f);
    banner.ShowIf([this]() { return m_Game.DrawOfferedBy().has_value() || m_Rematch != Rematch::None || m_MessageTimer > 0.f || (online() && m_Net.Reconnecting()); });

    m_BannerText = &banner.Add<Label>("", 14.f, Tone::Warning, true);

    Row& answers = banner.Add<Row>(8.f);
    answers.ShowIf([this]() {
        return m_Rematch == Rematch::Received || (m_Game.DrawOfferedBy().has_value() && (!online() || *m_Game.DrawOfferedBy() != m_PlayerColor));
    });

    answers.Add<Button>("Accept", ButtonKind::Primary, std::nullopt, [this]() {
        if (m_Rematch == Rematch::Received) {
            answerRematch(true);
            return;
        }

        leaveInspection();
        m_Game.AcceptDraw();

        if (online()) m_Net.Send(Net::EventType::DrawAccept);

        endGame();
    });
    answers.Add<Button>("Decline", ButtonKind::Standard, std::nullopt, [this]() {
        if (m_Rematch == Rematch::Received) {
            answerRematch(false);
            return;
        }

        m_Game.DeclineDraw();

        if (online()) m_Net.Send(Net::EventType::DrawDecline);

        notify("Draw declined");
    });

    Grid& tools = m_Panel->Add<Grid>(3, 8.f);
    tools.Add<Button>("Flip", ButtonKind::Tool, Icon::Flip, [this]() { m_BoardView.SetFlipped(!m_BoardView.Flipped()); });
    m_HintButton = &tools.Add<Button>("Hint", ButtonKind::Tool, Icon::Hint, [this]() { startSearch(true); });
    m_PlayButton = &tools.Add<Button>("Best Move", ButtonKind::Tool, Icon::Bolt, [this]() { playBestMove(); });
    m_InspectButton = &tools.Add<Button>("Inspect", ButtonKind::Tool, Icon::Inspect, [this]() { toggleInspection(); });
    tools.Add<Button>("New", ButtonKind::Tool, Icon::Plus, [this]() { openNewGame(); });
    tools.Add<Button>("Menu", ButtonKind::Tool, Icon::Menu, [this]() { openMenu(); });

    Grid& actions = m_Panel->Add<Grid>(2, 8.f);
    actions.ShowIf([this]() { return !(online() && m_Game.Over()); });
    m_DrawButton = &actions.Add<Button>("Offer Draw", ButtonKind::Standard, std::nullopt, [this]() { offerDraw(); });
    m_ResignButton = &actions.Add<Button>("Resign", ButtonKind::Danger, std::nullopt, [this]() { resign(); });

    m_RematchButton = &m_Panel->Add<Button>("Rematch", ButtonKind::Primary, std::nullopt, [this]() { offerRematch(); });
    m_RematchButton->ShowIf([this]() { return online() && m_Game.Over(); });

    m_BottomCard = &m_Panel->Add<Panel::PlayerCard>();
}

void Application::syncPanel() {
    const UI::Palette& pal = m_Ctx.Pal;

    const bool thinking = m_Engine.Thinking();
    const bool over = m_Game.Over();
    const bool inspecting = m_Game.Inspecting();

    const bool net = online();

    m_HintButton->SetEnabled(!over && !thinking && !net);

    m_PlayButton->SetEnabled(thinking || (!over && !net));
    m_PlayButton->SetLabel(thinking ? "Skip" : "Best Move");
    m_PlayButton->SetIcon(thinking ? UI::Icon::Skip : UI::Icon::Bolt);

    m_InspectButton->SetActive(inspecting);
    m_InspectButton->SetEnabled(!thinking && (!over || inspecting));

    m_DrawButton->SetEnabled(!over && !inspecting && !m_Game.DrawOfferedBy() && (!net || m_Net.Connected()));
    m_ResignButton->SetEnabled(!over && !inspecting);

    m_RematchButton->SetEnabled(m_Net.Connected() && m_Rematch == Rematch::None);

    if (net && m_Net.Reconnecting()) {
        m_BannerText->SetText("Connection lost, reconnecting... " + std::to_string(m_Net.ResumeSecondsLeft()) + "s");
    } else if (m_Rematch != Rematch::None) {
        m_BannerText->SetText(m_Rematch == Rematch::Sent ? "Rematch offer sent" : "Your opponent offers a rematch");
    } else if (const auto offer = m_Game.DrawOfferedBy()) {
        if (!net) m_BannerText->SetText(std::string(ColorName(*offer)) + " offers a draw");
        else m_BannerText->SetText(*offer == m_PlayerColor ? "Draw offer sent" : "Your opponent offers a draw");
    } else {
        m_BannerText->SetText(m_Message);
    }

    const auto describe = [&](Chess::PieceColor color) {
        Panel::PlayerCard::Data data;
        const bool turn = !over && m_Game.SideToMove() == color;
        const bool isEngine = m_Mode == GameMode::Engine && color == engineColor();

        data.Turn = turn;

        if (turn && thinking) data.Subtitle = "Thinking...";
        else if (m_Mode == GameMode::Engine) data.Subtitle = ColorName(color);
        else if (net) data.Subtitle = std::string(ColorName(color)) + (turn ? " - to move" : "");
        else if (turn) data.Subtitle = "To move";

        if (net) {
            const bool mine = color == m_PlayerColor;
            std::string name = mine ? m_Settings.PlayerName : m_Net.PeerName();

            if (name.empty()) name = mine ? "You" : "Opponent";

            // two letters of the name for the avatar
            std::string initials;
            for (const char c : name) {
                if (std::isalnum(static_cast<unsigned char>(c)) && initials.size() < 2) initials += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }

            if (!mine && !over && m_Net.Reconnecting()) data.Subtitle = "Reconnecting...";
            else if (!mine && !over && !m_Net.Connected()) data.Subtitle = "Disconnected";
            else if (!mine && m_Net.Unstable()) data.Subtitle = "Connection unstable...";

            data.Name = name;
            data.Avatar = mine ? "YOU" : (initials.empty() ? "?" : initials);
            data.AvatarFill = mine ? pal.Accent : pal.Raised;
            data.AvatarInk = mine ? pal.OnAccent : pal.Text;
        } else if (m_Mode == GameMode::Engine) {
            data.Name = isEngine ? std::string(Chess::EngineName) : "You";
            data.Avatar = isEngine ? "AI" : "YOU";
            data.AvatarFill = isEngine ? pal.Raised : pal.Accent;
            data.AvatarInk = isEngine ? pal.Text : pal.OnAccent;
        } else {
            const bool white = color == Chess::PieceColor::White;

            data.Name = ColorName(color);
            data.Avatar = white ? "W" : "B";
            data.AvatarFill = white ? pal.Text : pal.Inset;
            data.AvatarInk = white ? pal.Inset : pal.Text;
        }

        return data;
        };

    const Chess::PieceColor bottom = m_BoardView.Flipped() ? Chess::PieceColor::Black : Chess::PieceColor::White;

    m_TopCard->Set(describe(Chess::InvertColor(bottom)));
    m_BottomCard->Set(describe(bottom));
}

#pragma region Dialogs

void Application::showDialog(std::unique_ptr<UI::Dialog> dialog) {
    m_NextDialog = std::move(dialog);
    closeDialog();
}

void Application::closeDialog() {
    if (m_Dialog) m_Dialog->Close();
}

// Dialogs are only destroyed here, never from inside one of their own callbacks
void Application::reapDialogs() {
    if (m_Dialog && m_Dialog->Closed()) {
        if (m_Dialog.get() == m_WaitDialog) {
            m_WaitDialog = nullptr;
            m_WaitCode = m_WaitStatus = m_WaitInfo = nullptr;

            if (!m_NetBegun) m_Net.Close();
        }

        m_Dialog.reset();

        if (m_ConfigDirty) {
            applyEngineConfig();
            afterPositionChanged();
        }

        saveSettings();
    }

    if (m_NextDialog) {
        m_Dialog = std::move(m_NextDialog);
        m_Dialog->SetWindow(sf::Vector2f(m_WindowSize));
    }
}

#pragma region Frame

void Application::Update(float deltaTime, sf::Vector2i mousePosition) {
    reapDialogs();

    if (const std::optional<EngineRunner::Result> result = m_Engine.Take()) {
        if (result->Move && !m_Game.Over()) {
            if (result->Suggestion) m_BoardView.AddArrow(result->Move.StartingSquare, result->Move.TargetSquare);
            else applyMove(result->Move, true);
        }
    }

    pumpNetwork(deltaTime);

    if (m_ConfigDirty && (m_ConfigTimer -= deltaTime) <= 0.f) {
        applyEngineConfig();
        afterPositionChanged();
    }

    if (m_MessageTimer > 0.f) m_MessageTimer -= deltaTime;

    if (m_Rematch != Rematch::None && (m_RematchTimer -= deltaTime) <= 0.f) {
        m_Rematch = Rematch::None;
        notify("Rematch offer expired");
    }

    if (m_Game.DrawOfferedBy() && (m_DrawOfferTimer -= deltaTime) <= 0.f) {
        m_Game.DeclineDraw();
        notify("Draw offer expired");
    }

    m_BoardView.SetInteractive(humanCanMove());
    m_BoardView.SetInspection(m_Game.Inspecting());
    m_BoardView.SetShowCoordinates(m_Settings.ShowCoordinates);
    m_BoardView.Update(deltaTime);

    m_EvalBar.Update(deltaTime);

    const sf::Vector2f mouse(mousePosition);

    // widgets come and go (draw offers, the move list), so the panel is laid out again every frame
    syncPanel();
    m_Panel->SetRect(m_PanelRect);
    m_Panel->Update(deltaTime, m_Dialog ? UI::NoMouse : mouse);

    if (m_Dialog) m_Dialog->Update(deltaTime, mouse);

    reapDialogs();
}

void Application::Render(sf::RenderTarget& target, sf::Vector2i mousePosition) const {
    const UI::Painter painter(target, m_Ctx);

    target.clear(m_Ctx.Pal.Window);

    if (evalBarVisible()) {
        m_EvalBar.Render(target, m_EvalBarRect.position, m_EvalBarRect.size, m_BoardView.Flipped(), m_Theme->GetEvaluationBar());
    }

    m_BoardView.Render(painter, m_Dialog ? sf::Vector2i(-1, -1) : mousePosition);

    painter.Fill(m_PanelRect, m_Ctx.Pal.Panel);
    m_Panel->Render(painter);

    if (m_Dialog) m_Dialog->Render(painter);
}

#pragma region Input

bool Application::HandleKeyPressed(sf::Keyboard::Scancode key, bool control) {
    if (m_Dialog) {
        m_Dialog->OnKey(key, control);
        reapDialogs();
        return true;
    }


    return key != sf::Keyboard::Scancode::Escape;
}

void Application::HandleTextEntered(char32_t unicode) {
    if (m_Dialog) m_Dialog->OnText(unicode);
}

void Application::HandleMouseButtonPressed(sf::Mouse::Button button, sf::Vector2i position) {
    const sf::Vector2f point(position);

    if (m_Dialog) {
        if (button == sf::Mouse::Button::Left) m_Dialog->OnPressed(point);

        reapDialogs();
        return;
    }

    if (button == sf::Mouse::Button::Left) {
        m_BoardView.ClearAnnotations();

        if (m_PanelRect.contains(point)) {
            (void)m_Panel->OnPressed(point);
            return;
        }
    }

    m_BoardView.HandlePressed(button, position);
}

void Application::HandleMouseButtonReleased(sf::Mouse::Button button, sf::Vector2i position) {
    const sf::Vector2f point(position);

    if (m_Dialog) {
        if (button == sf::Mouse::Button::Left) m_Dialog->OnReleased(point);

        reapDialogs();
        return;
    }

    if (button == sf::Mouse::Button::Left) m_Panel->OnReleased(point);

    m_BoardView.HandleReleased(button, position);

    // a panel button may have asked for a dialog
    reapDialogs();
}

void Application::HandleMouseMoved(sf::Vector2i position) {
    if (!m_Dialog) m_BoardView.HandleMoved(position);
}

void Application::HandleMouseWheel(sf::Vector2i position, float delta) {
    const sf::Vector2f point(position);

    if (m_Dialog) m_Dialog->OnScroll(point, delta);
    else (void)m_Panel->OnScroll(point, delta);
}
