#include "GUI/Application.hpp"

#include <cstdio>

namespace {
    // 50ms below 1s, 250ms below 10s, 1.25s below 100s
    int ThinkTimeStep(int value, bool up) {
        const int probe = up ? value : value - 1;

        int step = Limits::ThinkTimeStep;
        for (int v = probe / 1000; v >= 1; v /= 10) step *= 5;

        return up ? value + step : value - step;
    }

    std::string FormatThinkTime(int ms) {
        if (ms < 1000) return std::to_string(ms) + " ms";

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(ms) / 1000.0);

        std::string seconds = buffer;
        while (seconds.back() == '0') seconds.pop_back();
        if (seconds.back() == '.') seconds.pop_back();

        return seconds + " s";
    }

    int HashStep(int value, bool up) {
        return up ? value * 2 : value / 2;
    }

    std::string FormatHash(int mb) {
        return mb >= 1024 && mb % 1024 == 0 ? std::to_string(mb / 1024) + " GB" : std::to_string(mb) + " MB";
    }

    int PlusMinusOne(int value, bool up) {
        return up ? value + 1 : value - 1;
    }

    int PlusMinusTen(int value, bool up) {
        return up ? value + 10 : value - 10;
    }

    std::string Plain(int value) {
        return std::to_string(value);
    }
}

void Application::openMenu() {
    using namespace UI;

    auto tabs = std::make_unique<TabView>(m_Ctx, [this]() { closeDialog(); });

    const auto addRow = [this](Column& page, const std::string& title, const std::string& description, std::unique_ptr<Widget> control) -> FormRow& {
        return page.Add<FormRow>(title, description, std::move(control));
    };

    const auto addToggle = [&](Column& page, const std::string& title, const std::string& description, bool& value, std::function<void()> onChange = {}) -> FormRow& {
        return addRow(page, title, description, std::make_unique<Toggle>(m_Ctx, value, std::move(onChange)));
    };

    const auto makeButton = [this](const std::string& label, std::function<void()> onClick) {
        auto button = std::make_unique<Button>(m_Ctx, label, ButtonKind::Standard, std::nullopt, std::move(onClick));
        button->Width(120.f);
        return button;
    };

    const auto makeChoices = [this](std::vector<Choice> choices, std::function<int()> get, std::function<void(int)> set) {
        return std::make_unique<ChoiceBar>(m_Ctx, std::move(choices), std::move(get), std::move(set), 200.f);
    };

    /* Game */ {
        auto page = std::make_unique<Column>(m_Ctx);

        addRow(*page, "New game", "Choose a mode, side and starting position", makeButton("New Game", [this]() { openNewGame(); }));

        addRow(*page, "Copy FEN", "Copy the current position to the clipboard", makeButton("Copy", [this]() {
            copyFen();
            closeDialog();
        }));

        addRow(*page, "Switch side to move", "Hand the turn to the other side in this position", makeButton("Switch", [this]() {
            flipSideToMove();
            closeDialog();
        }));

        tabs->AddPage("Game", std::move(page));
    }

    /* Engine */ {
        auto page = std::make_unique<Column>(m_Ctx);

        addRow(*page, "Search limit", "Think for a set time or to a set depth", makeChoices(
            {{"Time", "", std::nullopt}, {"Depth", "", std::nullopt}},
            [this]() { return m_Settings.LimitByDepth ? 1 : 0; },
            [this](int index) { m_Settings.LimitByDepth = index == 1; }
        ));

        addRow(*page, "Think time", "How long the engine thinks per move", MakeIntStepper(
            m_Ctx, m_Settings.ThinkTimeMs, Limits::ThinkTimeMin, Limits::ThinkTimeMax, ThinkTimeStep, FormatThinkTime, {}
        )).ShowIf([this]() { return !m_Settings.LimitByDepth; });

        addRow(*page, "Search depth", "Depth the engine searches to on every move", MakeIntStepper(
            m_Ctx, m_Settings.SearchDepth, Limits::DepthMin, Limits::DepthMax, PlusMinusOne, Plain, {}
        )).ShowIf([this]() { return m_Settings.LimitByDepth; });

        addRow(*page, "Evaluation depth", "Depth behind the evaluation bar", MakeIntStepper(
            m_Ctx, m_Settings.EvalDepth, Limits::DepthMin, Limits::DepthMax, PlusMinusOne, Plain, {}
        ));

        addToggle(*page, "Opening book", "Use the engine's own opening book", m_Settings.OwnBook);

        addToggle(*page, "Ponder", "Keep thinking while it's your turn", m_Settings.Ponder, [this]() {
            m_Engine.StopPonder();
            afterPositionChanged();
        });

        addToggle(*page, "Gotta Catch'em All!", "Captures all your pieces before mating", m_Settings.CatchAll, [this]() { scheduleEngineConfig(); });

        addRow(*page, "Hash size", "Memory for the transposition table", MakeIntStepper(
            m_Ctx, m_Settings.HashMB, 1, static_cast<int>(Platform::MaxHashMB()), HashStep, FormatHash, [this]() { scheduleEngineConfig(); }
        ));

        addRow(*page, "Threads", "Search threads, up to " + std::to_string(Platform::TotalThreads()), MakeIntStepper(
            m_Ctx, m_Settings.Threads, 1, static_cast<int>(Platform::TotalThreads()), PlusMinusOne, Plain, [this]() { scheduleEngineConfig(); }
        ));

        tabs->AddPage("Engine", std::move(page));
    }

    /* Display */ {
        auto page = std::make_unique<Column>(m_Ctx, 8.f);

        page->Add<Label>("BOARD THEME", 11.5f, Tone::Dim, true);

        TileGrid::Model themes;
        themes.Count = Themes::Count;
        themes.Columns = 2;
        themes.TileHeight = 52.f;
        themes.PreviewSize = {36.f, 36.f};
        themes.Name = [](std::size_t i) { return Themes::List()[i].Name; };
        themes.Selected = [this]() { return m_ThemeIdx; };
        themes.Select = [this](std::size_t i) { setTheme(i); };
        themes.DrawPreview = [](const Painter& painter, sf::FloatRect rect, std::size_t i) {
            const auto& colors = Themes::List()[i].Checkerboard;
            const float half = rect.size.x * 0.5f;

            painter.Fill(sf::FloatRect({rect.position.x, rect.position.y}, {half, half}), colors[0]);
            painter.Fill(sf::FloatRect({rect.position.x + half, rect.position.y}, {half, half}), colors[1]);
            painter.Fill(sf::FloatRect({rect.position.x, rect.position.y + half}, {half, half}), colors[1]);
            painter.Fill(sf::FloatRect({rect.position.x + half, rect.position.y + half}, {half, half}), colors[0]);
        };

        constexpr float PickerHeight = 2.3f * (52.f + 8.f);

        page->Add<ScrollView>(std::make_unique<TileGrid>(m_Ctx, std::move(themes)), PickerHeight);
        page->Add<Spacer>(6.f);
        page->Add<Label>("PIECES", 11.5f, Tone::Dim, true);

        TileGrid::Model pieces;
        pieces.Count = m_PieceSets.Count();
        pieces.Columns = 2;
        pieces.TileHeight = 52.f;
        pieces.PreviewSize = {72.f, 36.f};
        pieces.Name = [this](std::size_t i) { return m_PieceSets.At(i).Name; };
        pieces.Selected = [this]() { return m_PieceIdx; };
        pieces.Select = [this](std::size_t i) { (void)setPieceSet(i); };
        pieces.DrawPreview = [this](const Painter& painter, sf::FloatRect rect, std::size_t i) {
            const sf::Texture* preview = m_PieceSets.Preview(i);

            if (!preview) return;

            const float w = static_cast<float>(preview->getSize().x), h = static_cast<float>(preview->getSize().y);
            const float left = rect.position.x, top = rect.position.y;
            const float right = left + rect.size.x, bottom = top + rect.size.y;

            const sf::Vertex vertices[] = {
                sf::Vertex(sf::Vector2f(left, top), sf::Color::White, sf::Vector2f(0.f, 0.f)),
                sf::Vertex(sf::Vector2f(right, top), sf::Color::White, sf::Vector2f(w, 0.f)),
                sf::Vertex(sf::Vector2f(left, bottom), sf::Color::White, sf::Vector2f(0.f, h)),
                sf::Vertex(sf::Vector2f(right, bottom), sf::Color::White, sf::Vector2f(w, h))
            };

            painter.Target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip, preview);
        };

        page->Add<ScrollView>(std::make_unique<TileGrid>(m_Ctx, std::move(pieces)), PickerHeight);
        page->Add<Spacer>(6.f);

        addToggle(*page, "Evaluation bar", "Show the bar beside the board", m_Settings.ShowEvalBar, [this]() { layout(); });
        addToggle(*page, "Move list", "Show played moves in the side panel", m_Settings.ShowMoveList);
        addToggle(*page, "Coordinates", "Rank and file labels on the board", m_Settings.ShowCoordinates);

        tabs->AddPage("Display", std::move(page));
    }

    /* Sound */ {
        auto page = std::make_unique<Column>(m_Ctx);

        addToggle(*page, "Sound effects", "Play sounds for moves and results", m_Settings.SoundEnabled, [this]() { applySound(); });

        addRow(*page, "Volume", "How loud the sound effects are", MakeIntStepper(
            m_Ctx, m_Settings.Volume, Limits::VolumeMin, Limits::VolumeMax, PlusMinusTen,
            [](int value) { return std::to_string(value) + "%"; }, [this]() { applySound(); }
        ));

        tabs->AddPage("Sound", std::move(page));
    }

    /* Online */ {
        auto page = std::make_unique<Column>(m_Ctx);

        const auto makeField = [this](const std::string& placeholder, std::string& setting) {
            auto field = std::make_unique<TextField>(m_Ctx, placeholder);
            TextField* raw = field.get();

            field->SetValue(setting);
            field->SetOnChange([raw, &setting]() { setting = raw->Value(); });
            field->Width(220.f);

            return field;
        };

        addRow(*page, "Player name", "Shown to your opponent", makeField("Player", m_Settings.PlayerName));

        tabs->AddPage("Online", std::move(page));
    }

    showDialog(std::make_unique<Dialog>(m_Ctx, std::move(tabs), 720.f, 540.f, 0.f));
}

void Application::openNewGame() {
    using namespace UI;

    m_NewGameMode = m_Mode;
    m_NewGameBlack = m_Mode != GameMode::Local && m_PlayerColor == Chess::PieceColor::Black;

    auto content = std::make_unique<Column>(m_Ctx, 12.f);

    content->Add<Header>("New Game", [this]() { closeDialog(); });

    content->Add<Label>("MODE", 11.5f, Tone::Dim, true);
    content->Add<ChoiceBar>(
        std::vector<Choice>{
            {"Engine", "Play the computer", Icon::Cpu},
            {"Local", "Two players, one screen", Icon::Users},
            {"Online", "Play a friend with a code", Icon::Globe}
        },
        [this]() { return static_cast<int>(m_NewGameMode); },
        [this](int index) { m_NewGameMode = static_cast<GameMode>(index); }
    );

    const auto isOnline = [this]() { return m_NewGameMode == GameMode::Online; };
    const auto joining = [this]() { return m_NewGameMode == GameMode::Online && m_NewGameJoin; };
    const auto hosting = [this]() { return m_NewGameMode == GameMode::Online && !m_NewGameJoin; };
    const auto choosesSide = [this]() { return m_NewGameMode == GameMode::Engine || (m_NewGameMode == GameMode::Online && !m_NewGameJoin); };
    const auto choosesPosition = [this]() { return m_NewGameMode != GameMode::Online || !m_NewGameJoin; };

    content->Add<Label>("ONLINE", 11.5f, Tone::Dim, true).ShowIf(isOnline);
    content->Add<ChoiceBar>(
        std::vector<Choice>{{"Host", "", std::nullopt}, {"Join", "", std::nullopt}},
        [this]() { return m_NewGameJoin ? 1 : 0; },
        [this](int index) { m_NewGameJoin = index == 1; }
    ).ShowIf(isOnline);

    content->Add<Label>("PLAY AS", 11.5f, Tone::Dim, true).ShowIf(choosesSide);
    content->Add<ChoiceBar>(
        std::vector<Choice>{{"White", "", std::nullopt}, {"Black", "", std::nullopt}},
        [this]() { return m_NewGameBlack ? 1 : 0; },
        [this](int index) { m_NewGameBlack = index == 1; }
    ).ShowIf(choosesSide);

    content->Add<Label>("GAME CODE", 11.5f, Tone::Dim, true).ShowIf(joining);

    Row& codeRow = content->Add<Row>(8.f);
    codeRow.ShowIf(joining);
    TextField& codeField = codeRow.Add<TextField>("Enter the code, like K7Q-M2X");
    codeRow.Add<Button>("Paste", ButtonKind::Standard, std::nullopt, [&codeField]() { codeField.Paste(); }).Width(90.f);

    if (!m_JoinCode.empty()) codeField.SetValue(Net::FormatCode(m_JoinCode));

    // a guest plays the position the host picked
    content->Add<Label>("STARTING POSITION", 11.5f, Tone::Dim, true).ShowIf(choosesPosition);

    Row& position = content->Add<Row>(8.f);
    position.ShowIf(choosesPosition);
    TextField& field = position.Add<TextField>("Standard position, or paste a FEN");
    position.Add<Button>("Paste", ButtonKind::Standard, std::nullopt, [&field]() { field.Paste(); }).Width(90.f);

    // settings of the room, fixed for the whole game
    content->Add<Label>("ROOM", 11.5f, Tone::Dim, true).ShowIf(hosting);

    content->Add<FormRow>("Evaluation bar", "Shown to both players during the game", std::make_unique<Toggle>(m_Ctx, m_NewGameEval)).ShowIf(hosting);

    Label& error = content->Add<Label>("", 12.5f, Tone::Danger);

    Row& footer = content->Add<Row>(10.f);
    footer.Add<Button>("Cancel", ButtonKind::Standard, std::nullopt, [this]() { closeDialog(); });

    footer.Add<Button>("Start Game", ButtonKind::Primary, std::nullopt, [this, &field, &error]() {
        if (!field.Value().empty() && !Fen::Parse(field.Value())) {
            error.SetText("That doesn't look like a valid FEN.");
            return;
        }

        closeDialog();
        (void)startNewGame(m_NewGameMode, m_NewGameBlack, field.Value());
    }).ShowIf([this]() { return m_NewGameMode != GameMode::Online; });

    footer.Add<Button>("Host Game", ButtonKind::Primary, std::nullopt, [this, &field, &error]() {
        if (!field.Value().empty() && !Fen::Plausible(field.Value())) {
            error.SetText("That position can't be played online.");
            return;
        }

        closeDialog();
        startHosting(m_NewGameBlack, field.Value(), m_NewGameEval);
    }).ShowIf(hosting);

    footer.Add<Button>("Join Game", ButtonKind::Primary, std::nullopt, [this, &codeField, &error]() {
        const std::string code = Net::NormalizeCode(codeField.Value());

        if (code.empty()) {
            error.SetText("Codes are 6 letters and numbers, like K7Q-M2X.");
            return;
        }

        closeDialog();
        startJoining(code);
    }).ShowIf(joining);

    showDialog(std::make_unique<Dialog>(m_Ctx, std::move(content), 560.f));
}

void Application::openOnlineWait() {
    using namespace UI;

    auto content = std::make_unique<Column>(m_Ctx, 14.f);

    content->Add<Header>("Online Game", [this]() { closeDialog(); });

    const auto hosting = [this]() { return m_NetHost; };

    content->Add<Label>("Share this code with your opponent", 14.f, Tone::Dim).ShowIf(hosting);

    Label& code = content->Add<Label>("", 44.f, Tone::Normal, true);
    code.ShowIf(hosting);
    m_WaitCode = &code;

    content->Add<Button>("Copy Code", ButtonKind::Standard, std::nullopt, [this]() {
        sf::Clipboard::setString(Net::FormatCode(m_Net.Code()));
        m_Sfx.Play(Sfx::Notify);
    }).ShowIf(hosting);

    m_WaitStatus = &content->Add<Label>("", 15.f, Tone::Normal, true);
    m_WaitInfo = &content->Add<Label>("", 12.5f, Tone::Dim);

    Row& footer = content->Add<Row>(10.f);
    footer.Add<Button>("Cancel", ButtonKind::Standard, std::nullopt, [this]() { closeDialog(); });
    footer.Add<Button>("Try Again", ButtonKind::Primary, std::nullopt, [this]() {
        if (m_NetHost) m_Net.Host(Net::GenerateCode(), netConfig());
        else m_Net.Join(m_JoinCode, netConfig());
    }).ShowIf([this]() { return m_Net.GetState() == Net::Session::State::Closed; });

    auto dialog = std::make_unique<Dialog>(m_Ctx, std::move(content), 480.f);

    m_WaitDialog = dialog.get();
    showDialog(std::move(dialog));
}

void Application::openConfirm(std::string title, std::string text, std::string confirmLabel, std::function<void()> onConfirm) {
    using namespace UI;

    auto content = std::make_unique<Column>(m_Ctx, 14.f);

    content->Add<Header>(std::move(title), [this]() { closeDialog(); });
    content->Add<Label>(std::move(text), 14.f, Tone::Dim);

    Row& footer = content->Add<Row>(10.f);
    footer.Add<Button>("Cancel", ButtonKind::Standard, std::nullopt, [this]() { closeDialog(); });
    footer.Add<Button>(std::move(confirmLabel), ButtonKind::Danger, std::nullopt, [this, onConfirm = std::move(onConfirm)]() {
        closeDialog();
        onConfirm();
    });

    showDialog(std::make_unique<Dialog>(m_Ctx, std::move(content), 420.f));
}
