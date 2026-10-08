#include <iostream>
#include <sstream>

#include "GUI/Application.hpp"

#pragma region Setup

Application::Application() {
    Chess::Init();

    m_Board.SetHashSize(128);
    m_Board.SetCatchAll(false);

    loadFen(Chess::DefaultFEN);
}

void Application::loadFen(const std::string& fen) {
    if (fen.empty()) return;

    joinThreads();

    m_Board.NewGame();

    m_SideToMove = m_Board.LoadFromFen(fen);
    m_Board.SetEngineColor(m_SideToMove);
    m_Board.ClearHash();

    updateEvaluation();

    m_GameOver = m_Flipped = false;
    m_LastMove = Chess::Move();

    startPonder();

    m_GameOverTimer = 0.f;
    m_GameOverResult = GameOverResult::None;
    m_ParticleSystem.RemoveAll();
}

Application::~Application() {
    joinThreads();
}

void Application::SetTargetSize(sf::Vector2u size) {
    const float windowHeight = static_cast<float>(size.y);
    const float windowWidth = static_cast<float>(size.x);

    m_SquareSize = windowHeight / static_cast<float>(Chess::Ranks);

    const float boardWidth = Chess::Files * m_SquareSize;

    m_EvaluationBarWidth = windowHeight * 0.075f;
    m_PanelWidth = windowHeight * 0.05f;

    const float padding_y = windowHeight * 0.006f;
    const float padding_x = m_PanelWidth * 0.11f;

    const float panelStartPos = m_EvaluationBarWidth + boardWidth;
    const float buttonSize = m_PanelWidth - 2.f * padding_x;

    const float currentXPos = panelStartPos + padding_x;
    float currentYPos = 0.f + padding_y;

    m_ButtonPanel.RemoveAll();

    // flip board button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        0,
        [this](ButtonPanel::Button&) -> void {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
                m_SideToMove = Chess::InvertColor(m_SideToMove);
                m_Board.FlipSideToMove();
                updateEvaluation();
            } else {
                m_Flipped ^= true;
            }
        },
        std::array<std::string, 2>{"Flip Board", "Flip Side"}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // get engine move button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        1,
        [this](ButtonPanel::Button&) -> void {
            if (m_EngineThinking || m_GameOver) return;

            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
                pollEngineMove(true);
            } else {
                pollEngineMove();
            }
        },
        std::array<std::string, 2>{"Play Best Move", "Suggest Best Move"}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // skip search button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        2,
        [this](ButtonPanel::Button&) -> void {
            if (m_EngineThinking) m_Board.CancelSearch();
        },
        std::array<std::string, 2>{"Skip Search", ""}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // up button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        4,
        [this](ButtonPanel::Button&) -> void {
            if (
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl) ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::RControl)
            ) {
                ++m_EvaluationDepth;
                updateEvaluation();
                m_Popup = Popup("evaluation depth: " + std::to_string(m_EvaluationDepth));
            } else {
                m_EngineThinkTimeMs += 50;
                m_Popup = Popup("think time: " + std::to_string(m_EngineThinkTimeMs));
            }
        },
        std::array<std::string, 2>{"Search Time +", "Eval Depth +"}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // down button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        5,
        [this](ButtonPanel::Button&) -> void {
            if (
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl) ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::RControl)
            ) {
                if (m_EvaluationDepth > 1) --m_EvaluationDepth;
                updateEvaluation();
                m_Popup = Popup("evaluation depth: " + std::to_string(m_EvaluationDepth));
            } else {
                m_EngineThinkTimeMs = std::max(50, m_EngineThinkTimeMs - 50);
                m_Popup = Popup("think time: " + std::to_string(m_EngineThinkTimeMs));
            }
        },
        std::array<std::string, 2>{"Search Time -", "Eval Depth -"}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // own book button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        7 - m_UseOwnBook,
        [this](ButtonPanel::Button& button) -> void {
            m_UseOwnBook ^= 1;

            button.TextureIndex = 7 - m_UseOwnBook;
            m_Popup = Popup("own book: " + std::string(m_UseOwnBook ? "true" : "false"));
        },
        std::array<std::string, 2>{"Own Book", ""}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // ponder button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        9 - m_Ponder,
        [this](ButtonPanel::Button& button) -> void {
            m_Ponder ^= 1;

            if (!m_EngineThinking) {
                if (m_Ponder) startPonder();
                else stopPonder();
            }

            button.TextureIndex = 9 - m_Ponder;
            m_Popup = Popup("ponder: " + std::string(m_Ponder ? "true" : "false"));
        },
        std::array<std::string, 2>{"Ponder", ""}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // catch all button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        11 - m_Board.GetCatchAll(),
        [this](ButtonPanel::Button& button) -> void {
            joinThreads();

            m_Board.SetCatchAll(!m_Board.GetCatchAll());
            m_Board.ClearHash();

            if (!m_GameOver) updateEvaluation();

            button.TextureIndex = 11 - m_Board.GetCatchAll();
            m_Popup = Popup("catch all mode: " + std::string(m_Board.GetCatchAll() ? "true" : "false"));
        },
        std::array<std::string, 2>{"Gotta Catch'em All!", ""}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // inspection mode button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, currentYPos,
        buttonSize, buttonSize,
        13 - m_InspectionMode,
        [this](ButtonPanel::Button& button) -> void {
            if (!m_EngineThinking && (!m_GameOver || m_InspectionMode)) {
                joinThreads();

                if (m_InspectionMode) {
                    const bool oldFlipped = m_Flipped;

                    loadFen(m_InspectionEntryState.Fen);
                    m_LastMove = m_InspectionEntryState.LastMove;
                    stopPonder();

                    m_Flipped = oldFlipped;
                } else {
                    m_InspectionEntryState = GameState(m_Board.GetFen(m_SideToMove), m_LastMove);
                }

                m_InspectionMode ^= 1;
                button.TextureIndex = 13 - m_InspectionMode;

                m_Popup = Popup("inspection mode: " + std::string(m_InspectionMode ? "true" : "false"));
            }
        },
        std::array<std::string, 2>{"Inspection Mode", ""}
    ));

    currentYPos += buttonSize + padding_y * 2.f;

    // board reset / theme cycle button
    m_ButtonPanel.AddButton(ButtonPanel::Button(
        currentXPos, windowHeight - buttonSize - padding_y,
        buttonSize, buttonSize,
        3,
        [this](ButtonPanel::Button&) -> void {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
                m_CurrentThemeIdx = (m_CurrentThemeIdx + 1) % Themes::Count;
                m_Theme = Themes::Factories[m_CurrentThemeIdx]();
                m_Popup = Popup("Theme: " + std::string(Themes::Names[m_CurrentThemeIdx]));
            } else {
                m_PromotionSelectionActive = false;
                loadFen(Chess::DefaultFEN);
            }
        },
        std::array<std::string, 2>{"Reset Board", "Cycle Themes"}
    ));
}

void Application::joinThreads() {
    m_Board.CancelSearch();
    if (m_PonderThread.joinable()) m_PonderThread.join();
    if (m_SearchThread.joinable()) m_SearchThread.join();
}

#pragma region Resources

bool Application::LoadResources(const std::filesystem::path& root) {
    const std::filesystem::path texturePath = root / "Textures";

    if (!m_PieceTexture.loadFromFile(texturePath / "Pieces.png")) [[unlikely]] {
        std::cerr << "Failed to load piece texture" << std::endl;
        return false;
    }

    if (!m_IconTexture.loadFromFile(texturePath / "Icons.png")) [[unlikely]] {
        std::cerr << "Failed to load piece texture" << std::endl;
        return false;
    }

    if (!m_Font.openFromFile(root / "Fonts" / "Font.ttf")) [[unlikely]] {
        std::cerr << "Failed to open font" << std::endl;
        return false;
    }

    m_Font.setSmooth(false);

    const std::filesystem::path soundPath = root / "Sounds";

    if (!m_SfxPlayer.LoadFromFile(Sfx::Capture, soundPath / "Capture.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Check, soundPath / "Check.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::GameEnd, soundPath / "Game End.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::MoveSelf, soundPath / "Move Self.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::MoveOpponent, soundPath / "Move Opp.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Promotion, soundPath / "Promotion.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Notify, soundPath / "Notify.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::Illegal, soundPath / "Illegal.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    if (!m_SfxPlayer.LoadFromFile(Sfx::SpecialMove, soundPath / "Special Move.wav")) [[unlikely]] {
        std::cerr << "Failed to load sound" << std::endl;
        return false;
    }

    // no errors in loading the assets
    return true;
}

#pragma region Events

void Application::HandleKeyPressed(sf::Keyboard::Scancode key) {
    if (key == sf::Keyboard::Scancode::V) {
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
            m_PromotionSelectionActive = false;
            loadFen(sf::Clipboard::getString().toAnsiString());
        }
    }

    else if (key == sf::Keyboard::Scancode::C) {
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scancode::LControl)) {
            sf::Clipboard::setString(m_Board.GetFen(m_SideToMove));
        }
    }
}

void Application::HandleMouseButtonPressed(sf::Mouse::Button button, sf::Vector2i position) {
    if (button == sf::Mouse::Button::Left) {
        m_Markers = 0ull;
        m_Arrows.clear();

        if (m_PromotionSelectionActive) {
            if (const int hit = hitTestPromotionMenu(position)) {
                commitPromotion(Chess::PromotionFlags[hit - 1]);
            }

            m_PromotionSelectionActive = false;
            m_SelectedPiece = Chess::NullPos;
            m_LegalMovesForSelectedPiece.clear();
        } else {
            if (m_ButtonPanel.Clicked()) return;

            onMouseButtonSignal(position, false);

            if (m_GameOver && m_ParticleSystem.GetParticles().empty()) m_GameOverResult = GameOverResult::None;
        }
    }

    else if (button == sf::Mouse::Button::Right) {
        const auto [rank, file] = mapMousePosToCoordinates(position);
        m_CurrentlyDrawingArrow.Start = Chess::To2DIndex(rank, file);
    }
}

void Application::HandleMouseButtonReleased(sf::Mouse::Button button, sf::Vector2i position) {
    if (button == sf::Mouse::Button::Left) {
        if (!m_PromotionSelectionActive) onMouseButtonSignal(position, true);
    }

    else if (button == sf::Mouse::Button::Right) {
        const auto [rank, file] = mapMousePosToCoordinates(position);
        const int idx = Chess::To2DIndex(rank, file);

        m_CurrentlyDrawingArrow.End = idx;

        if (m_CurrentlyDrawingArrow.Start == m_CurrentlyDrawingArrow.End) {
            m_Markers ^= Chess::IndexToMask(idx); // toggle marker
        } else if (m_CurrentlyDrawingArrow) {
            toggleArrow(m_CurrentlyDrawingArrow.Start, m_CurrentlyDrawingArrow.End);
        }

        m_CurrentlyDrawingArrow = Arrow();
    }
}

std::vector<Arrow>::iterator Application::findArrow(uint8_t start, uint8_t end) {
    return std::find_if(m_Arrows.begin(), m_Arrows.end(),
        [start, end](const Arrow& existing) -> bool {
            return existing.Start == start && existing.End == end;
        }
    );
}

void Application::toggleArrow(uint8_t start, uint8_t end) {
    auto it = findArrow(start, end);

    if (it != m_Arrows.end()) {
        m_Arrows.erase(it);
    } else {
        m_Arrows.push_back(Arrow{start, end});
    }

    m_Markers |= (
        Chess::IndexToMask(start) |
        Chess::IndexToMask(end)
    );
}

void Application::addArrow(uint8_t start, uint8_t end) {
    if (findArrow(start, end) != m_Arrows.end()) return;

    m_Arrows.push_back(Arrow{start, end});

    m_Markers |= (
        Chess::IndexToMask(start) |
        Chess::IndexToMask(end)
    );
}

void Application::HandleMouseMoved(sf::Vector2i position) {
    if (m_PromotionSelectionActive) return;

    if (hasSelectedPiece()) {
        const sf::Vector2f delta(
            static_cast<float>(position.x - m_LastMousePosition.x),
            static_cast<float>(position.y - m_LastMousePosition.y)
        );

        const float speed = std::sqrtf(delta.x * delta.x + delta.y * delta.y);

        if (speed > 0.5f) {
            constexpr float MaxTilt = 0.45f; // ~26 degrees
            const float targetTilt = (delta.x / speed) * std::min(speed / 18.f, 1.f) * MaxTilt;
            m_DragTilt = targetTilt;
        }
    } else {
        const float panelLeft = m_EvaluationBarWidth + Chess::Files * m_SquareSize;

        m_ButtonPanel.HandleMouseMoved(
            position,
            position.x > panelLeft && position.x < panelLeft + m_PanelWidth
        );
    }

    if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right)) {
        const auto [rank, file] = mapMousePosToCoordinates(position);
        m_CurrentlyDrawingArrow.End = Chess::To2DIndex(rank, file);
    }

    m_LastMousePosition = position;
}

void Application::HandleMouseLeftWindow() {
    m_ButtonPanel.HandleMouseMoved({-1, -1}, false);
}

void Application::commitPromotion(Chess::MoveFlag promotionFlag) {
    const uint8_t landingPos = m_SelectedPiece + (m_SideToMove == Chess::PieceColor::White ? -Chess::Files : Chess::Files);

    for (const Chess::Move& m : m_LegalMovesForSelectedPiece) {
        if (
            m.StartingSquare == m_SelectedPiece && m.TargetSquare == landingPos &&
            Chess::HasFlag(m.Flag, promotionFlag)
        ) {
            doMove(m, true);
            if (!m_GameOver && !m_InspectionMode) pollEngineMove();

            return;
        }
    }
}

int Application::hitTestPromotionMenu(sf::Vector2i mousePos) const {
    if (!m_PromotionSelectionActive) return -1;

    const float menuX = mapFile(Chess::ToFile(m_SelectedPiece)) * m_SquareSize + m_EvaluationBarWidth;
    const float visualRank = mapRank(Chess::ToRank(m_SelectedPiece) + (m_SideToMove == Chess::PieceColor::White ? -1 : 1));

    const bool openDown = visualRank < Chess::Ranks / 2;
    const float cardTop = openDown ? visualRank * m_SquareSize : (visualRank - 3.f) * m_SquareSize;

    for (int i = 0; i < 4; ++i) {
        const float slotY = openDown ? (visualRank + i) * m_SquareSize : (visualRank - i) * m_SquareSize;

        if (
            mousePos.x >= menuX && mousePos.x < menuX + m_SquareSize &&
            mousePos.y >= slotY && mousePos.y < slotY + m_SquareSize
        ) {
            return i + 1;
        }
    }

    return 0;
}

#pragma region Gameplay

void Application::pollEngineMove(bool suggestOnly) {
    // return;

    if (m_GameOver || m_EngineThinking) return;

    joinThreads();

    m_EngineThinking = true;
    m_SuggestingMove = suggestOnly;

    m_Board.SetEngineColor(m_SideToMove);

    // clear hash if engine color has changed this game
    // since hash values are misleading now
    if (m_Board.GetCatchAll()) {
        if (m_SideToMove != m_LastEngineColor) {
            m_Board.ClearHash();
        }

        m_LastEngineColor = m_SideToMove;
    }

    Chess::Board searchBoard = m_Board;
    Chess::PieceColor searchSideToMove = m_SideToMove;

    m_SearchThread = std::thread([searchBoard, searchSideToMove, this]() -> void {
        const Chess::Move bestMove = searchBoard.FindBestMoveByTime(searchSideToMove, m_EngineThinkTimeMs, m_UseOwnBook);

        m_PendingEngineMove = bestMove;
        m_EngineThinking = false;
    });
}

void Application::updateEvaluation() {
    m_EvaluationBar.SetEval(m_Board.GetConfidence(m_SideToMove, m_EvaluationDepth));
}

void Application::doMove(Chess::Move move, bool animate) {
    m_Board.DoMove(move);
    m_SideToMove = Chess::InvertColor(m_SideToMove);

    // check endgame
    if (
        m_Board.FiftyMoveRule() ||
        m_Board.isThreefoldRepetition() ||
        m_Board.HasInsufficientMaterial() ||
        !m_Board.HasAnyLegalMoves(m_SideToMove)
    ) {
        m_SfxPlayer.Play(Sfx::GameEnd);

        if (!m_InspectionMode) {
            if (m_Board.FiftyMoveRule()) {
                m_GameOverResult = GameOverResult::DrawFiftyMove;
            } else if (m_Board.HasInsufficientMaterial()) {
                m_GameOverResult = GameOverResult::DrawInsufficient;
            } else if (m_Board.isThreefoldRepetition()) {
                m_GameOverResult = GameOverResult::DrawRepetition;
            } else if (!m_Board.isUnderCheck(m_SideToMove)) {
                m_GameOverResult = GameOverResult::Stalemate;
            } else {
                m_GameOverResult = GameOverResult::Checkmate;
            }

            m_GameOverTimer = 0.001f;

            m_ParticleSystem.Spawn(
                90, sf::FloatRect(
                    sf::Vector2f(m_EvaluationBarWidth, 0.f),
                    sf::Vector2f(Chess::Files * m_SquareSize, Chess::Ranks * m_SquareSize)
                )
            );
        }

        m_GameOver = true;
    }

    // play sound effects
    if (HasFlag(move.Flag, Chess::MoveFlag::Check)) {
        m_SfxPlayer.Play(Sfx::Check);
    } else if (isCastle(move.Flag)) {
        m_SfxPlayer.Play(Sfx::SpecialMove);

        const bool isKingSide = HasFlag(move.Flag, Chess::MoveFlag::CastleKingSide);
        const int rank = Chess::ToRank(move.StartingSquare);

        const int rookFrom = Chess::To2DIndex(rank, isKingSide ? 7 : 0);
        const int rookTo = Chess::To2DIndex(rank, isKingSide ? 5 : 3);

        m_PieceAnimations.emplace_back(rookFrom, rookTo);
    } else if (isPromotion(move.Flag)) {
        m_SfxPlayer.Play(Sfx::Promotion);
    } else if (HasFlag(move.Flag, Chess::MoveFlag::Capture)) {
        m_SfxPlayer.Play(Sfx::Capture);
    } else {
        const bool isSelfMove = (m_SideToMove == Chess::PieceColor::Black) ^ m_Flipped;
        m_SfxPlayer.Play(isSelfMove ? Sfx::MoveSelf : Sfx::MoveOpponent);
    }

    m_LastMove = move;

    if (Chess::HasFlag(move.Flag, Chess::MoveFlag::Capture)) {
        const auto it = std::find_if(m_PieceAnimations.begin(), m_PieceAnimations.end(),
            [&move](const PieceMoveAnim& anim) {
                return anim.End == move.TargetSquare;
            }
        );

        if (it != m_PieceAnimations.end()) {
            m_PieceAnimations.erase(it);
        }
    }

    if (animate) m_PieceAnimations.emplace_back(move.StartingSquare, move.TargetSquare);

    updateEvaluation();
}

void Application::onMouseButtonSignal(sf::Vector2i position, bool released) {
    if (
        m_GameOver ||
        position.x < m_EvaluationBarWidth ||
        position.x > m_EvaluationBarWidth + Chess::Files * m_SquareSize
    ) return;

    const auto [rank, file] = mapMousePosToCoordinates(position);
    const uint8_t idx = Chess::To2DIndex(rank, file);

    if (released) {
        if (hasSelectedPiece() && idx != m_SelectedPiece) dropPiece(idx);
    } else {
        const bool clickedOwnPiece = (m_Board.GetOccupancyMap(m_SideToMove) & Chess::IndexToMask(idx)) != 0ull;

        if (hasSelectedPiece()) dropPiece(idx, true, !clickedOwnPiece);

        pickPiece(idx);
    }
}

void Application::pickPiece(int idx) {
    if (m_EngineThinking) return;

    // if current idx has a piece of mover's color
    if (m_Board.GetOccupancyMap(m_SideToMove) & Chess::IndexToMask(idx)) {
        m_SelectedPiece = idx;
        m_LegalMovesForSelectedPiece = m_Board.GetLegalMoves(idx);
    }
}

void Application::dropPiece(int idx, bool animate, bool playIllegalSfx) {
    const auto it = std::find(m_LegalMovesForSelectedPiece.begin(), m_LegalMovesForSelectedPiece.end(),
        Chess::Move(m_SelectedPiece, idx)
    );

    if (it != m_LegalMovesForSelectedPiece.end()) {
        if (Chess::isPromotion(it->Flag)) {
            m_PromotionSelectionActive = true;
            m_SfxPlayer.Play(Sfx::Notify);
            return;
        }

        doMove(*it, animate);

        if (!m_GameOver && !m_InspectionMode) pollEngineMove();
    } else if (idx != m_SelectedPiece && playIllegalSfx) {
        m_SfxPlayer.Play(Sfx::Illegal);
    }

    m_SelectedPiece = Chess::NullPos;
    m_LegalMovesForSelectedPiece.clear();
}

#pragma region Ponder

void Application::startPonder() {
    if (!m_Ponder || m_GameOver || m_InspectionMode) return;

    Chess::Board ponderBoard = m_Board;
    Chess::PieceColor ponderSideToMove = m_SideToMove;
    Chess::Move bestMove = m_PendingEngineMove;

    // do opponents best response first
    ponderBoard.DoMove(ponderBoard.FindBestMoveByDepth(ponderSideToMove, 1));
    ponderSideToMove = Chess::InvertColor(ponderSideToMove);

    if (ponderBoard.HasAnyLegalMoves(ponderSideToMove)) {
        m_PonderThread = std::thread([ponderBoard, ponderSideToMove, bestMove]() -> void {
            (void)ponderBoard.FindBestMoveByTime(ponderSideToMove, std::numeric_limits<int>::max(), false);
        });
    }
}

void Application::stopPonder() {
    m_Board.CancelSearch();
    if (m_PonderThread.joinable()) m_PonderThread.join();
}

#pragma region Update

void Application::Update(float deltaTime) {
    if (m_PendingEngineMove) {
        if (m_SuggestingMove) {
            addArrow(m_PendingEngineMove.StartingSquare, m_PendingEngineMove.TargetSquare);
        } else {
            doMove(m_PendingEngineMove, true);
            startPonder();
        }

        m_SuggestingMove = false;
        m_PendingEngineMove = Chess::Move();
    }

    const float decrement = deltaTime / 0.16f;

    for (auto it = m_PieceAnimations.begin(); it != m_PieceAnimations.end(); ) {
        PieceMoveAnim& anim = *it;

        anim.Timer -= decrement;

        if (anim.Timer <= 0.f) {
            it = m_PieceAnimations.erase(it);
        } else {
            ++it;
        }
    }

    if (m_Popup.Timer > 0.f) {
        m_Popup.Timer -= deltaTime;
        if (m_Popup.Timer <= 0.f) m_Popup.Info.clear();
    }

    if (m_GameOverTimer > 0.f) {
        m_GameOverTimer += deltaTime;
    }

    m_ParticleSystem.Update(deltaTime);
    m_EvaluationBar.Update(deltaTime);
    m_ButtonPanel.Update(deltaTime);

    m_InspectionModeOverlay_t = Utils::ExponentiallyMoveTo(
        m_InspectionModeOverlay_t, m_InspectionMode, 7.5f * deltaTime
    );

    m_DragTilt = Utils::ExponentiallyMoveTo(
        m_DragTilt, 0.f, 7.5f * deltaTime
    );
}

#pragma region Render

void Application::renderBoard(sf::RenderTarget& target) const {
    sf::VertexArray vertices(sf::PrimitiveType::Triangles, Chess::Files * Chess::Ranks * 6u);

    unsigned int vertexIndex = 0u;

    for (int rank = 0; rank < Chess::Ranks; ++rank) {
        const float y = static_cast<float>(rank) * m_SquareSize;

        for (int file = 0; file < Chess::Files; ++file) {
            const float x = static_cast<float>(file) * m_SquareSize + m_EvaluationBarWidth;

            const sf::Color color = m_Theme->GetCheckerboard()[(file + rank) % 2];

            const sf::Vertex topleft = sf::Vertex(sf::Vector2f(x, y), color);
            const sf::Vertex topright = sf::Vertex(sf::Vector2f(x + m_SquareSize, y), color);
            const sf::Vertex bottomright = sf::Vertex(sf::Vector2f(x + m_SquareSize, y + m_SquareSize), color);
            const sf::Vertex bottomleft = sf::Vertex(sf::Vector2f(x, y + m_SquareSize), color);

            vertices[vertexIndex++] = topleft;
            vertices[vertexIndex++] = topright;
            vertices[vertexIndex++] = bottomright;

            vertices[vertexIndex++] = topleft;
            vertices[vertexIndex++] = bottomright;
            vertices[vertexIndex++] = bottomleft;
        }
    }

    target.draw(vertices);

    // ranks and files

    const unsigned int charSize = static_cast<unsigned int>(m_SquareSize * 0.175f);
    const float padding = m_SquareSize * 0.06f;

    // ranks
    for (unsigned int rank = 0; rank < Chess::Ranks; ++rank) {
        const float y = static_cast<float>(rank) * m_SquareSize;
        const char rankChar = (m_Flipped ? rank : Chess::Ranks - 1 - rank) + '1';

        sf::Text text(m_Font, rankChar, charSize);
        text.setStyle(sf::Text::Style::Bold);
        text.setFillColor(m_Theme->GetCheckerboard()[(rank + m_Flipped) % 2]);

        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin(bounds.position);

        const float x = m_Flipped
            ? m_EvaluationBarWidth + padding
            : m_EvaluationBarWidth + Chess::Files * m_SquareSize - bounds.size.x - padding;

        text.setPosition(sf::Vector2f(x, y + padding));
        target.draw(text);
    }

    // files
    for (unsigned int file = 0; file < Chess::Files; ++file) {
        const float x = m_EvaluationBarWidth + static_cast<float>(file) * m_SquareSize;
        const char fileChar = (m_Flipped ? Chess::Files - 1 - file : file) + 'a';

        sf::Text text(m_Font, fileChar, charSize);
        text.setStyle(sf::Text::Style::Bold);
        text.setFillColor(m_Theme->GetCheckerboard()[file % 2]);

        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin(bounds.position);

        text.setPosition(sf::Vector2f(
            x + m_SquareSize - bounds.size.x - padding,
            Chess::Ranks * m_SquareSize - bounds.size.y - padding
        ));

        target.draw(text);
    }
}

void Application::renderSquareHighlight(sf::RenderTarget& target, uint8_t square, sf::Color color) const {
    if ((m_Markers & Chess::IndexToMask(square)) == 0ull) {
        Utils::RenderQuad(
            target, color,
            sf::Vector2f(
                mapFile(Chess::ToFile(square)) * m_SquareSize + m_EvaluationBarWidth,
                mapRank(Chess::ToRank(square)) * m_SquareSize
            ),
            sf::Vector2f(m_SquareSize, m_SquareSize)
        );
    }
}

void Application::renderPiece(sf::RenderTarget& target, Chess::Piece piece, float x, float y, float angle) const {
    const float PieceTextureWidth = static_cast<float>(m_PieceTexture.getSize().x) / 6.f;
    const float PieceTextureHeight = static_cast<float>(m_PieceTexture.getSize().y) / 2.f;

    const float tx = static_cast<float>(piece.Type) * PieceTextureWidth;
    const float ty = static_cast<float>(piece.Color) * PieceTextureHeight;

    const float halfSquareSize = m_SquareSize * 0.5f;

    const float cx = x + halfSquareSize;
    const float cy = y + halfSquareSize;

    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);

    const auto rotate = [&](float px, float py) -> sf::Vector2f {
        const float dx = px - cx;
        const float dy = py - cy;

        const float rx = dx * cosA - dy * sinA;
        const float ry = dx * sinA + dy * cosA;

        return sf::Vector2f(cx + rx, cy + ry);
        };

    const sf::Vertex vertices[] = {
        sf::Vertex(rotate(x, y), sf::Color::White, sf::Vector2f(tx, ty)),
        sf::Vertex(rotate(x + m_SquareSize, y), sf::Color::White, sf::Vector2f(tx + PieceTextureWidth, ty)),
        sf::Vertex(rotate(x, y + m_SquareSize), sf::Color::White, sf::Vector2f(tx, ty + PieceTextureHeight)),
        sf::Vertex(rotate(x + m_SquareSize, y + m_SquareSize), sf::Color::White, sf::Vector2f(tx + PieceTextureWidth, ty + PieceTextureHeight))
    };

    target.draw(vertices, 4, sf::PrimitiveType::TriangleStrip, &m_PieceTexture);
}

void Application::renderPieces(sf::RenderTarget& target, bool mouseHeld) const {
    const uint64_t occupancyMap = m_Board.GetOccupancyMap();

    const int selectedPiece = mouseHeld ? m_SelectedPiece : Chess::NullPos;

    for (int rank = 0; rank < Chess::Ranks; ++rank) {
        const float y = static_cast<float>(mapRank(rank)) * m_SquareSize;
        const int rankIdx = Chess::Files * rank;

        for (int file = 0; file < Chess::Files; ++file) {
            const float x = static_cast<float>(mapFile(file)) * m_SquareSize + m_EvaluationBarWidth;
            const int idx = rankIdx + file;

            // skip rendering this piece if it's selected
            // it will be rendered at the mouse position instead
            if (idx == selectedPiece) continue;

            // skip empty cell
            if ((occupancyMap & Chess::IndexToMask(idx)) == 0ull) continue;

            // skip rendering this piece if its being animated
            if (std::any_of(m_PieceAnimations.begin(), m_PieceAnimations.end(),
                [idx](const PieceMoveAnim& anim) -> bool {
                    return anim.End == idx;
                }
            )) continue;

            renderPiece(target, m_Board.GetPieceAt(idx), x, y);
        }
    }
}

void Application::renderLegalMoves(sf::RenderTarget& target, sf::Vector2i mousePosition) const {
    const auto [mouseRank, mouseFile] = mapMousePosToCoordinates(mousePosition);
    const std::size_t hoveredIdx = Chess::To2DIndex(mouseRank, mouseFile);

    for (const Chess::Move& move : m_LegalMovesForSelectedPiece) {
        const int rank = mapRank(Chess::ToRank(move.TargetSquare));
        const int file = mapFile(Chess::ToFile(move.TargetSquare));

        const float y = rank * m_SquareSize;
        const float x = file * m_SquareSize + m_EvaluationBarWidth;

        if (m_Theme->DrawHoveredLegalSq() && move.TargetSquare == hoveredIdx) {
            Utils::RenderQuad(target, m_Theme->GetLegalMoveHighlight(), sf::Vector2f(x, y), sf::Vector2f(m_SquareSize, m_SquareSize));
        } else if (m_Board.GetOccupancyMap(Chess::InvertColor(m_SideToMove)) & Chess::IndexToMask(move.TargetSquare)) {
            m_Theme->RenderCaptureOverlay(target, sf::Vector2f(x, y), m_SquareSize);
        } else {
            const float circleRadius = m_SquareSize * 0.15f;

            sf::CircleShape circle(circleRadius);

            const float offset = m_SquareSize * 0.5f - circleRadius;
            circle.setPosition(sf::Vector2f(x + offset, y + offset));
            circle.setFillColor(m_Theme->GetLegalMoveHighlight());

            target.draw(circle);
        }
    }

    if (
        hasSelectedPiece() &&
        m_Theme->DrawHoveredSqOutline() &&
        mousePosition.x > m_EvaluationBarWidth &&
        mousePosition.x < (Chess::Files * m_SquareSize + m_EvaluationBarWidth) &&
        sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)
    ) {
        const int file = mapFile(Chess::ToFile(hoveredIdx));
        const int rank = mapRank(Chess::ToRank(hoveredIdx));

        Utils::RenderFrame(
            target, sf::Color(255, 255, 255, 166),
            sf::Vector2f(m_EvaluationBarWidth + file * m_SquareSize, rank * m_SquareSize),
            sf::Vector2f(m_SquareSize, m_SquareSize),
            0.05f
        );
    }
}

void Application::renderPopup(sf::RenderTarget& target) const {
    const float progress = std::min(1.f, m_Popup.Timer / 0.15f);
    const float eased = progress * progress * (3.f - 2.f * progress);

    sf::Text text(m_Font, m_Popup.Info, 15);
    text.setStyle(sf::Text::Style::Bold);

    const sf::FloatRect bounds = text.getLocalBounds();
    text.setOrigin(bounds.position);

    const float paddingH = 18.f;
    const float paddingV = 24.f;
    const float pillW = bounds.size.x + paddingH * 2.f;
    const float pillH = bounds.size.y + paddingV * 2.f;
    const float margin = 12.f;

    const float targetX = static_cast<float>(target.getSize().x) - pillW - margin;
    const float targetY = static_cast<float>(target.getSize().y) - pillH - margin;

    const float slideOffset = (1.f - eased) * 18.f;
    const sf::Vector2f pillPos(targetX, targetY + slideOffset);

    Utils::RenderRoundedQuad(target, Utils::ConvertAlpha(m_Theme->GetOverlayPill(), eased), pillPos, sf::Vector2f(pillW, pillH), 0.4f);

    text.setFillColor(Utils::ConvertAlpha(m_Theme->GetTextPrimary(), eased));
    text.setPosition(sf::Vector2f(pillPos.x + paddingH, pillPos.y + paddingV));

    target.draw(text);
}

void Application::renderCheckmateOverlay(sf::RenderTarget& target) const {
    const float W = static_cast<float>(target.getSize().x);
    const float H = static_cast<float>(target.getSize().y);

    const float rawP = std::min(1.f, m_GameOverTimer / 0.45f);
    const float eased = rawP * rawP * (3.f - 2.f * rawP);

    // bg overlay
    Utils::RenderQuad(
        target, Utils::ConvertAlpha(m_Theme->GetSurfaceOverlay(), eased),
        sf::Vector2f(m_EvaluationBarWidth, 0.f),
        sf::Vector2f(Chess::Files * m_SquareSize, H)
    );

    m_ParticleSystem.Render(target, m_Theme->GetParticles(), eased);

    // result card
    std::string titleStr, subStr;
    switch (m_GameOverResult) {
    case GameOverResult::Checkmate: titleStr = "CHECKMATE"; subStr = "The game is over."; break;
    case GameOverResult::Stalemate: titleStr = "STALEMATE"; subStr = "No legal moves - it's a draw."; break;
    case GameOverResult::DrawFiftyMove: titleStr = "DRAW"; subStr = "50-move rule."; break;
    case GameOverResult::DrawRepetition: titleStr = "DRAW"; subStr = "Threefold repetition."; break;
    case GameOverResult::DrawInsufficient: titleStr = "DRAW"; subStr = "Insufficient material."; break;
    default: return;
    }

    const unsigned int titleSize = static_cast<unsigned int>(m_SquareSize * 0.55f);
    const unsigned int subSize = static_cast<unsigned int>(m_SquareSize * 0.22f);

    sf::Text titleText(m_Font, titleStr, titleSize);
    titleText.setStyle(sf::Text::Style::Bold);
    sf::Text subText(m_Font, subStr, subSize);

    const sf::FloatRect tb = titleText.getLocalBounds();
    const sf::FloatRect sb = subText.getLocalBounds();

    const float cardPadH = m_SquareSize * 0.60f;
    const float cardPadV = m_SquareSize * 0.50f;
    const float cardGap = m_SquareSize * 0.20f;
    const float cardW = std::max(tb.size.x, sb.size.x) + cardPadH * 2.f;
    const float cardH = tb.size.y + sb.size.y + cardGap + cardPadV * 2.f;

    const float boardCX = m_EvaluationBarWidth + Chess::Files * m_SquareSize * 0.5f;
    const float boardCY = H * 0.5f;

    const float slideY = (1.f - eased) * m_SquareSize * 0.55f;

    const sf::Vector2f cardPos(boardCX - cardW * 0.5f, boardCY - cardH * 0.5f + slideY);

    Utils::RenderRoundedQuad(target, Utils::ConvertAlpha(m_Theme->GetSurfaceRaised(), eased), cardPos, sf::Vector2f(cardW, cardH), 0.1f);

    // decorative separator line
    const float lineW = cardW * 0.5f;
    const float lineY = cardPos.y + cardPadV + tb.size.y + cardGap * 0.45f;

    Utils::RenderQuad(target, Utils::ConvertAlpha(m_Theme->GetTextSecondary(), eased * 0.45f),
        sf::Vector2f(boardCX - lineW * 0.5f, lineY),
        sf::Vector2f(lineW, 1.5f)
    );

    // title text - horizontally centred
    titleText.setOrigin(sf::Vector2f(tb.position.x + tb.size.x * 0.5f, tb.position.y));
    titleText.setPosition(sf::Vector2f(boardCX, cardPos.y + cardPadV));
    titleText.setFillColor(Utils::ConvertAlpha(m_Theme->GetTextPrimary(), eased));
    target.draw(titleText);

    // subtitle text
    subText.setOrigin(sf::Vector2f(sb.position.x + sb.size.x * 0.5f, sb.position.y));
    subText.setPosition(sf::Vector2f(boardCX, cardPos.y + cardPadV + tb.size.y + cardGap));
    subText.setFillColor(Utils::ConvertAlpha(m_Theme->GetTextSecondary(), eased));
    target.draw(subText);
}

void Application::renderPromotionMenu(sf::RenderTarget& target, sf::Vector2i mousePos) const {
    const int boardRank = Chess::ToRank(m_SelectedPiece) + (m_SideToMove == Chess::PieceColor::White ? -1 : 1);
    const int visualRank = mapRank(boardRank);

    const float menuX = mapFile(Chess::ToFile(m_SelectedPiece)) * m_SquareSize + m_EvaluationBarWidth;

    const bool openDown = visualRank < Chess::Ranks / 2;

    const float boardLeft = m_EvaluationBarWidth;
    const float boardRight = boardLeft + Chess::Files * m_SquareSize;
    const float boardHeight = static_cast<float>(target.getSize().y);

    const float cardTop = openDown ? visualRank * m_SquareSize : (visualRank - 3.f) * m_SquareSize;
    const float cardBottom = cardTop + 4.f * m_SquareSize;

    const sf::Color dim(0, 0, 0, 130);

    if (menuX > boardLeft) {
        Utils::RenderQuad(target, dim,
            sf::Vector2f(boardLeft, 0.f), sf::Vector2f(menuX - boardLeft, boardHeight)
        );
    }

    if (menuX + m_SquareSize < boardRight) {
        Utils::RenderQuad(target, dim,
            sf::Vector2f(menuX + m_SquareSize, 0.f), sf::Vector2f(boardRight - menuX - m_SquareSize, boardHeight)
        );
    }

    if (cardTop > 0.f) {
        Utils::RenderQuad(target, dim,
            sf::Vector2f(menuX, 0.f), sf::Vector2f(m_SquareSize, cardTop)
        );
    }

    if (cardBottom < boardHeight) {
        Utils::RenderQuad(target, dim,
            sf::Vector2f(menuX, cardBottom), sf::Vector2f(m_SquareSize, boardHeight - cardBottom)
        );
    }

    Utils::RenderRoundedQuad(
        target, sf::Color(255, 255, 255, 245),
        sf::Vector2f(menuX, cardTop), sf::Vector2f(m_SquareSize, 4.f * m_SquareSize),
        0.12f
    );

    unsigned int i = 0u;
    for (Chess::PieceType promotionType : Chess::PromotionTypes) {
        const float y = openDown ? (visualRank + i) * m_SquareSize : (visualRank - i) * m_SquareSize;
        renderPiece(target, Chess::Piece{promotionType, m_SideToMove}, menuX, y);

        ++i;
    }
}

void Application::renderArrow(sf::RenderTarget& target, Arrow arrow) const {
    const float arrowWidth = m_SquareSize * 0.185f;
    const float headLength = m_SquareSize * 0.40f;
    const float headWidth = m_SquareSize * 0.45f;

    const float halfSquareSize = m_SquareSize * 0.5f;

    const sf::Vector2f startPos(
        mapFile(Chess::ToFile(arrow.Start)) * m_SquareSize + m_EvaluationBarWidth + halfSquareSize,
        mapRank(Chess::ToRank(arrow.Start)) * m_SquareSize + halfSquareSize
    );

    const sf::Vector2f endPos(
        mapFile(Chess::ToFile(arrow.End)) * m_SquareSize + m_EvaluationBarWidth + halfSquareSize,
        mapRank(Chess::ToRank(arrow.End)) * m_SquareSize + halfSquareSize
    );

    const int rankDiff = std::abs(
        Chess::ToRank(arrow.End) - Chess::ToRank(arrow.Start)
    );

    const int fileDiff = std::abs(
        Chess::ToFile(arrow.End) - Chess::ToFile(arrow.Start)
    );

    // knight move
    if (
        m_Theme->DrawKnightArrowAsL() &&
        ((rankDiff == 2 && fileDiff == 1) || (rankDiff == 1 && fileDiff == 2))
    ) {
        const sf::Vector2f midPos = (rankDiff == 2 && fileDiff == 1)
            ? sf::Vector2f(startPos.x, endPos.y) : sf::Vector2f(endPos.x, startPos.y);

        const sf::Vector2f dir1 = midPos - startPos;
        const float len1 = std::sqrtf(dir1.x * dir1.x + dir1.y * dir1.y);

        const sf::Vector2f dir2 = endPos - midPos;
        const float len2 = std::sqrtf(dir2.x * dir2.x + dir2.y * dir2.y);

        if (len1 > 0.f && len2 > 0.f) {
            const sf::Vector2f norm1 = sf::Vector2f(dir1.x / len1, dir1.y / len1);
            const sf::Vector2f perp1 = sf::Vector2f(-norm1.y, norm1.x);

            const sf::Vector2f norm2 = sf::Vector2f(dir2.x / len2, dir2.y / len2);
            const sf::Vector2f perp2 = sf::Vector2f(-norm2.y, norm2.x);

            const sf::Vector2f seg1End = midPos + norm1 * (arrowWidth * 0.5f);

            const sf::Vector2f seg2Start = midPos + norm2 * (arrowWidth * 0.5f);
            const sf::Vector2f shaftEnd = endPos - norm2 * headLength;

            // first segment
            const sf::Vertex shaft1[] = {
                sf::Vertex(startPos + perp1 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(startPos - perp1 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(seg1End + perp1 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(seg1End - perp1 * (arrowWidth * 0.5f), m_Theme->GetArrow())
            };

            target.draw(shaft1, 4, sf::PrimitiveType::TriangleStrip);

            // second segment
            const sf::Vertex shaft2[] = {
                sf::Vertex(seg2Start + perp2 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(seg2Start - perp2 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(shaftEnd + perp2 * (arrowWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(shaftEnd - perp2 * (arrowWidth * 0.5f), m_Theme->GetArrow())
            };

            target.draw(shaft2, 4, sf::PrimitiveType::TriangleStrip);

            // head
            const sf::Vertex head[] = {
                sf::Vertex(shaftEnd + perp2 * (headWidth * 0.5f), m_Theme->GetArrow()),
                sf::Vertex(endPos, m_Theme->GetArrow()),
                sf::Vertex(shaftEnd - perp2 * (headWidth * 0.5f), m_Theme->GetArrow())
            };

            target.draw(head, 3, sf::PrimitiveType::Triangles);
        }
    } else {
        const sf::Vector2f dir = endPos - startPos;
        const float len = std::sqrtf(dir.x * dir.x + dir.y * dir.y);

        if (len == 0.f) return;

        const sf::Vector2f norm = sf::Vector2f(dir.x / len, dir.y / len);
        const sf::Vector2f perp = sf::Vector2f(-norm.y, norm.x);

        const float shortenStart = m_SquareSize * 0.15f;
        const float shortenEnd = m_SquareSize * 0.15f;

        const sf::Vector2f adjustedStart = startPos + norm * shortenStart;
        const sf::Vector2f adjustedEnd = endPos - norm * shortenEnd;
        const sf::Vector2f shaftEnd = adjustedEnd - norm * headLength;

        // shaft
        const sf::Vertex shaft[] = {
            sf::Vertex(adjustedStart + perp * (arrowWidth * 0.5f), m_Theme->GetArrow()),
            sf::Vertex(adjustedStart - perp * (arrowWidth * 0.5f), m_Theme->GetArrow()),
            sf::Vertex(shaftEnd + perp * (arrowWidth * 0.5f), m_Theme->GetArrow()),
            sf::Vertex(shaftEnd - perp * (arrowWidth * 0.5f), m_Theme->GetArrow())
        };

        target.draw(shaft, 4, sf::PrimitiveType::TriangleStrip);

        // head
        const sf::Vertex head[] = {
            sf::Vertex(shaftEnd + perp * (headWidth * 0.5f), m_Theme->GetArrow()),
            sf::Vertex(adjustedEnd, m_Theme->GetArrow()),
            sf::Vertex(shaftEnd - perp * (headWidth * 0.5f), m_Theme->GetArrow())
        };

        target.draw(head, 3, sf::PrimitiveType::Triangles);
    }
}

void Application::Render(sf::RenderTarget& target, sf::Vector2i mousePosition) const {
    const float halfSquareSize = m_SquareSize * 0.5f;
    const bool mouseHeld = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

    target.clear(m_Theme->GetBackground());

    /* Board */ {
        renderBoard(target);

        m_EvaluationBar.Render(
            target,
            sf::Vector2f(), sf::Vector2f(m_EvaluationBarWidth, target.getSize().y),
            m_Flipped, m_Theme->GetEvaluationBar()
        );

        if (m_InspectionModeOverlay_t > 0.f) {
            Utils::RenderQuad(
                target, sf::Color(255, 245, 255, 38 * m_InspectionModeOverlay_t),
                sf::Vector2f(m_EvaluationBarWidth, 0.f), sf::Vector2f(Chess::Files, Chess::Ranks) * m_SquareSize
            );
        }
    }

    /* Highlights */ {
        // markers
        for (uint64_t tmp = m_Markers; tmp; tmp &= tmp - 1) {
            const int idx = Chess::MaskToIndex(tmp);

            m_Theme->RenderMarker(
                target,
                sf::Vector2f(
                    mapFile(Chess::ToFile(idx)) * m_SquareSize + m_EvaluationBarWidth,
                    mapRank(Chess::ToRank(idx)) * m_SquareSize
                ),
                m_SquareSize
            );
        }

        // move history
        if (m_LastMove) {
            renderSquareHighlight(target, m_LastMove.StartingSquare, m_Theme->GetLastMoveHighlight());

            if (m_Theme->DrawLastMoveTargetIfCapturable() || !std::any_of(
                m_LegalMovesForSelectedPiece.begin(), m_LegalMovesForSelectedPiece.end(),
                [this](Chess::Move move) -> bool {
                    return move.TargetSquare == m_LastMove.TargetSquare;
                }
            )) {
                renderSquareHighlight(target, m_LastMove.TargetSquare, m_Theme->GetLastMoveHighlight());
            }
        }

        // selected piece
        if (hasSelectedPiece()) {
            if (m_Theme->DrawSelectedPieceGhost()) {
                renderPiece(
                    target, m_Board.GetPieceAt(m_SelectedPiece),
                    mapFile(Chess::ToFile(m_SelectedPiece)) * m_SquareSize + m_EvaluationBarWidth,
                    mapRank(Chess::ToRank(m_SelectedPiece)) * m_SquareSize
                );
            }

            renderSquareHighlight(target, m_SelectedPiece, m_Theme->GetSelectedPiece());
        }

        renderLegalMoves(target, mousePosition);
    }

    /* Pieces */ {
        // all pieces
        renderPieces(target, mouseHeld);

        // piece animations
        for (const PieceMoveAnim& anim : m_PieceAnimations) {
            const sf::Vector2f start = sf::Vector2f(
                mapFile(Chess::ToFile(anim.End)), mapRank(Chess::ToRank(anim.End))
            );

            const sf::Vector2f end = sf::Vector2f(
                mapFile(Chess::ToFile(anim.Start)), mapRank(Chess::ToRank(anim.Start))
            );

            const sf::Vector2f dir = end - start;
            const sf::Vector2f position = (start + dir * anim.Timer) * m_SquareSize;

            const float len = std::sqrtf(dir.x * dir.x + dir.y * dir.y);
            const float animTilt = len ? -dir.x / len * 1.2f * anim.Timer * (1.f - anim.Timer) : 0.f;

            renderPiece(
                target, m_Board.GetPieceAt(anim.End),
                position.x + m_EvaluationBarWidth, position.y,
                animTilt
            );
        }
    }

    /* UI */ {
        // arrows
        if (m_CurrentlyDrawingArrow) renderArrow(target, m_CurrentlyDrawingArrow);

        for (Arrow arrow : m_Arrows) {
            renderArrow(target, arrow);
        }

        // buttons
        m_ButtonPanel.Render(
            target,
            m_Theme->GetBackground(), m_Theme->GetTextPrimary(), m_Theme->GetOverlayPill(),
            sf::Vector2f(m_EvaluationBarWidth + Chess::Files * m_SquareSize, 0.f),
            sf::Vector2f(m_PanelWidth, target.getSize().y),
            m_Font, m_IconTexture, m_SquareSize
        );

        // dragged piece
        if (mouseHeld && hasSelectedPiece()) {
            renderPiece(
                target, m_Board.GetPieceAt(m_SelectedPiece),
                mousePosition.x - halfSquareSize, mousePosition.y - halfSquareSize,
                m_DragTilt
            );
        }

        if (m_GameOverResult != GameOverResult::None) {
            renderCheckmateOverlay(target);
        }

        if (m_PromotionSelectionActive) {
            renderPromotionMenu(target, mousePosition);
        }

        if (m_Popup.isActive()) {
            renderPopup(target);
        }
    }
}