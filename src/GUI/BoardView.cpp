#include "GUI/BoardView.hpp"

#include <algorithm>
#include <cmath>

namespace {
    constexpr float AnimationDuration = 0.16f;
    constexpr float IllegalFlashDuration = 1.f;
    constexpr float GameOverFadeIn = 0.45f;
    constexpr std::size_t GameOverParticles = 150;

    float Smooth(float t) {
        return t * t * (3.f - 2.f * t);
    }

    sf::Vector2f Normalized(sf::Vector2f v, float length) {
        return sf::Vector2f(v.x / length, v.y / length);
    }

    float Length(sf::Vector2f v) {
        return std::sqrt(v.x * v.x + v.y * v.y);
    }
}

#pragma region Geometry and state

std::optional<int> BoardView::squareAt(sf::Vector2i position) const noexcept {
    if (m_Square <= 0.f) return std::nullopt;

    const float fx = (static_cast<float>(position.x) - m_Origin.x) / m_Square;
    const float fy = (static_cast<float>(position.y) - m_Origin.y) / m_Square;

    if (fx < 0.f || fy < 0.f || fx >= Chess::Files || fy >= Chess::Ranks) return std::nullopt;

    return Chess::To2DIndex(mapRank(static_cast<int>(fy)), mapFile(static_cast<int>(fx)));
}

sf::Vector2f BoardView::squarePosition(int idx) const noexcept {
    return sf::Vector2f(
        m_Origin.x + static_cast<float>(mapFile(Chess::ToFile(idx))) * m_Square,
        m_Origin.y + static_cast<float>(mapRank(Chess::ToRank(idx))) * m_Square
    );
}

void BoardView::SetGeometry(sf::Vector2f origin, float squareSize) {
    m_Origin = origin;
    m_Square = squareSize;
    m_CoordinatesDirty = true;

    rebuildBoard();
}

void BoardView::SetTheme(const Themes::Theme& theme) {
    m_Theme = &theme;
    m_IllegalTimer = 0.f;
    m_CoordinatesDirty = true;

    rebuildBoard();
}

void BoardView::rebuildBoard() {
    if (!m_Theme || m_Square <= 0.f) return;

    m_BoardVertices.resize(Chess::Ranks * Chess::Files * 6);

    std::size_t index = 0;

    for (int rank = 0; rank < Chess::Ranks; ++rank) {
        for (int file = 0; file < Chess::Files; ++file) {
            const sf::Color color = m_Theme->GetCheckerboard()[(file + rank) % 2];

            const float x = m_Origin.x + static_cast<float>(file) * m_Square;
            const float y = m_Origin.y + static_cast<float>(rank) * m_Square;

            const sf::Vertex topLeft(sf::Vector2f(x, y), color);
            const sf::Vertex topRight(sf::Vector2f(x + m_Square, y), color);
            const sf::Vertex bottomRight(sf::Vector2f(x + m_Square, y + m_Square), color);
            const sf::Vertex bottomLeft(sf::Vector2f(x, y + m_Square), color);

            m_BoardVertices[index++] = topLeft;
            m_BoardVertices[index++] = topRight;
            m_BoardVertices[index++] = bottomRight;

            m_BoardVertices[index++] = topLeft;
            m_BoardVertices[index++] = bottomRight;
            m_BoardVertices[index++] = bottomLeft;
        }
    }
}

void BoardView::NewGame() {
    clearSelection();

    m_PromotionActive = false;
    ClearAnnotations();
    m_Animations.clear();
    m_IllegalTimer = 0.f;
    m_GameOverTimer = 0.f;
    m_Particles.RemoveAll();
    m_DragTilt = 0.f;
}

void BoardView::ClearAnnotations() {
    m_Markers = 0ull;
    m_Arrows.clear();
    m_DrawingArrow = Arrow();
}

void BoardView::toggleArrow(uint8_t start, uint8_t end) {
    const auto it = std::find_if(m_Arrows.begin(), m_Arrows.end(), [=](const Arrow& a) {
        return a.Start == start && a.End == end;
    });

    if (it != m_Arrows.end()) m_Arrows.erase(it);
    else m_Arrows.push_back({start, end});
}

void BoardView::AddArrow(uint8_t start, uint8_t end) {
    const bool exists = std::any_of(m_Arrows.begin(), m_Arrows.end(), [=](const Arrow& a) {
        return a.Start == start && a.End == end;
    });

    if (!exists) m_Arrows.push_back({start, end});
}

void BoardView::AnimateMove(Chess::Move move, bool animate) {
    if (Chess::isCastle(move.Flag)) {
        const int rank = Chess::ToRank(move.StartingSquare);
        const bool kingSide = Chess::HasFlag(move.Flag, Chess::MoveFlag::CastleKingSide);

        m_Animations.emplace_back(
            static_cast<uint8_t>(Chess::To2DIndex(rank, kingSide ? 7 : 0)),
            static_cast<uint8_t>(Chess::To2DIndex(rank, kingSide ? 5 : 3))
        );
    }

    if (Chess::HasFlag(move.Flag, Chess::MoveFlag::Capture)) {
        std::erase_if(m_Animations, [&](const PieceMoveAnim& anim) { return anim.End == move.TargetSquare; });
    }

    if (animate) m_Animations.emplace_back(move.StartingSquare, move.TargetSquare);
}

void BoardView::ShowGameOver(std::string title, std::string subtitle, bool confetti) {
    m_GameOverTitle = std::move(title);
    m_GameOverSubtitle = std::move(subtitle);
    m_GameOverTimer = 0.0001f;

    if (confetti) {
        m_Particles.Spawn(GameOverParticles, sf::FloatRect(m_Origin, sf::Vector2f(Chess::Files * m_Square, Chess::Ranks * m_Square)));
    } else {
        m_Particles.RemoveAll();
    }
}

void BoardView::Update(float deltaTime) {
    const float decrement = deltaTime / AnimationDuration;

    std::erase_if(m_Animations, [decrement](PieceMoveAnim& anim) {
        anim.Timer -= decrement;
        return anim.Timer <= 0.f;
    });

    if (m_GameOverTimer > 0.f) m_GameOverTimer += deltaTime;

    if (m_IllegalTimer > 0.f) m_IllegalTimer = std::max(0.f, m_IllegalTimer - deltaTime);

    m_Particles.Update(deltaTime);

    m_InspectionT = Utils::ExponentiallyMoveTo(m_InspectionT, m_Inspection ? 1.f : 0.f, 7.5f * deltaTime);
    m_DragTilt = Utils::ExponentiallyMoveTo(m_DragTilt, 0.f, 7.5f * deltaTime);
}

#pragma region Input

void BoardView::select(int idx) {
    if (!m_Interactive) return;

    if (m_Game.Board().GetOccupancyMap(m_Game.SideToMove()) & Chess::IndexToMask(idx)) {
        m_Selected = static_cast<uint8_t>(idx);
        m_LegalMoves = m_Game.Board().GetLegalMoves(idx);
    }
}

void BoardView::clearSelection() {
    m_Selected = Chess::NullPos;
    m_LegalMoves.clear();
}

void BoardView::drop(int idx, bool animate, bool playIllegalSfx) {
    m_IllegalTimer = 0.f;

    const auto it = std::find(m_LegalMoves.begin(), m_LegalMoves.end(), Chess::Move(m_Selected, static_cast<uint8_t>(idx)));

    if (it != m_LegalMoves.end()) {
        if (Chess::isPromotion(it->Flag)) {
            // the piece stays selected until a promotion piece is chosen
            m_PromotionActive = true;
            m_PromotionTarget = static_cast<uint8_t>(idx);

            if (OnPromotionPrompt) OnPromotionPrompt();

            return;
        }

        const Chess::Move move = *it;

        clearSelection();

        if (OnMove) OnMove(move, animate);

        return;
    }

    if (idx != m_Selected && playIllegalSfx) {
        if (OnIllegalMove) OnIllegalMove();

        if (m_Theme->DrawIllegalMoveHighlight()) {
            m_IllegalTimer = IllegalFlashDuration;
            m_IllegalSquare = m_Selected;
        }
    }

    clearSelection();
}

void BoardView::commitPromotion(Chess::MoveFlag flag) {
    for (const Chess::Move& move : m_LegalMoves) {
        if (move.StartingSquare != m_Selected || move.TargetSquare != m_PromotionTarget) continue;
        if (!Chess::HasFlag(move.Flag, flag)) continue;

        const Chess::Move chosen = move;

        m_PromotionActive = false;
        clearSelection();

        if (OnMove) OnMove(chosen, true);

        return;
    }
}

void BoardView::signal(int idx, bool released) {
    if (hasSelection() && idx != m_Selected) drop(idx, !released, released);

    if (!released) select(idx);
}

int BoardView::hitTestPromotionMenu(sf::Vector2i position) const noexcept {
    if (!m_PromotionActive) return 0;

    const int visualRank = mapRank(Chess::ToRank(m_PromotionTarget));
    const float menuX = squarePosition(m_PromotionTarget).x;
    const bool openDown = visualRank < Chess::Ranks / 2;

    for (int i = 0; i < 4; ++i) {
        const float y = m_Origin.y + static_cast<float>(openDown ? visualRank + i : visualRank - i) * m_Square;

        if (sf::FloatRect({menuX, y}, {m_Square, m_Square}).contains(sf::Vector2f(position))) return i + 1;
    }

    return 0;
}

void BoardView::HandlePressed(sf::Mouse::Button button, sf::Vector2i position) {
    if (button == sf::Mouse::Button::Left) {
        if (m_PromotionActive) {
            if (const int hit = hitTestPromotionMenu(position)) {
                commitPromotion(Chess::PromotionFlags[hit - 1]);
            } else {
                m_PromotionActive = false;
                clearSelection();
            }

            return;
        }

        if (const std::optional<int> idx = squareAt(position)) signal(*idx, false);

        // the game over card goes away with a click once it has faded in and the confetti has settled
        if (m_GameOverTimer > GameOverFadeIn && m_Particles.GetParticles().empty()) m_GameOverTimer = 0.f;
    } else if (button == sf::Mouse::Button::Right) {
        m_DrawingArrow = Arrow();

        if (const std::optional<int> idx = squareAt(position)) m_DrawingArrow.Start = static_cast<uint8_t>(*idx);
    }
}

void BoardView::HandleReleased(sf::Mouse::Button button, sf::Vector2i position) {
    if (button == sf::Mouse::Button::Left) {
        if (m_PromotionActive) return;

        if (const std::optional<int> idx = squareAt(position)) signal(*idx, true);
    } else if (button == sf::Mouse::Button::Right) {
        const std::optional<int> idx = squareAt(position);

        if (idx && m_DrawingArrow.Start == *idx) {
            m_Markers ^= Chess::IndexToMask(*idx);
        } else if (idx && m_DrawingArrow.Start != Arrow::NullPos) {
            toggleArrow(m_DrawingArrow.Start, static_cast<uint8_t>(*idx));
        }

        m_DrawingArrow = Arrow();
    }
}

void BoardView::HandleMoved(sf::Vector2i position) {
    if (m_PromotionActive) return;

    if (hasSelection()) {
        const sf::Vector2f delta(
            static_cast<float>(position.x - m_LastMouse.x),
            static_cast<float>(position.y - m_LastMouse.y)
        );

        const float speed = Length(delta);

        if (speed > 0.5f) {
            constexpr float MaxTilt = 0.45f; // ~26 degrees
            m_DragTilt = (delta.x / speed) * std::min(speed / 18.f, 1.f) * MaxTilt;
        }
    }

    if (m_DrawingArrow.Start != Arrow::NullPos && sf::Mouse::isButtonPressed(sf::Mouse::Button::Right)) {
        const std::optional<int> idx = squareAt(position);
        m_DrawingArrow.End = idx ? static_cast<uint8_t>(*idx) : Arrow::NullPos;
    }

    m_LastMouse = position;
}

#pragma region Rendering

void BoardView::rebuildCoordinates(const sf::Font& font) const {
    m_Coordinates.clear();
    m_Coordinates.reserve(Chess::Ranks + Chess::Files);
    m_CoordinatesDirty = false;

    const unsigned int charSize = static_cast<unsigned int>(m_Square * 0.175f);
    const float padding = m_Square * 0.06f;

    // ranks
    for (int rank = 0; rank < Chess::Ranks; ++rank) {
        const char rankChar = static_cast<char>((m_Flipped ? rank : Chess::Ranks - 1 - rank) + '1');

        sf::Text& text = m_Coordinates.emplace_back(font, std::string(1, rankChar), charSize);
        text.setStyle(sf::Text::Style::Bold);
        text.setFillColor(m_Theme->GetCheckerboard()[(rank + m_Flipped) % 2]);

        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin(bounds.position);

        const float x = m_Flipped
            ? m_Origin.x + padding
            : m_Origin.x + Chess::Files * m_Square - bounds.size.x - padding;

        text.setPosition(sf::Vector2f(x, m_Origin.y + static_cast<float>(rank) * m_Square + padding));
    }

    // files
    for (int file = 0; file < Chess::Files; ++file) {
        const char fileChar = static_cast<char>((m_Flipped ? Chess::Files - 1 - file : file) + 'a');

        sf::Text& text = m_Coordinates.emplace_back(font, std::string(1, fileChar), charSize);
        text.setStyle(sf::Text::Style::Bold);
        text.setFillColor(m_Theme->GetCheckerboard()[file % 2]);

        const sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin(bounds.position);

        text.setPosition(sf::Vector2f(
            m_Origin.x + static_cast<float>(file) * m_Square + m_Square - bounds.size.x - padding,
            m_Origin.y + Chess::Ranks * m_Square - bounds.size.y - padding
        ));
    }
}

void BoardView::renderBoard(const UI::Painter& painter) const {
    sf::RenderTarget& target = painter.Target;

    target.draw(m_BoardVertices);

    if (!m_ShowCoordinates) return;

    if (m_CoordinatesDirty) rebuildCoordinates(*painter.Ctx.Font);

    for (const sf::Text& text : m_Coordinates) target.draw(text);
}

void BoardView::appendPiece(std::vector<sf::Vertex>& out, Chess::Piece piece, float x, float y, float angle) const {
    const float cellWidth = static_cast<float>(m_Pieces.getSize().x) / 6.f;
    const float cellHeight = static_cast<float>(m_Pieces.getSize().y) / 2.f;

    const float tx = static_cast<float>(piece.Type) * cellWidth;
    const float ty = static_cast<float>(piece.Color) * cellHeight;

    sf::Vector2f topLeft(x, y), topRight(x + m_Square, y), bottomLeft(x, y + m_Square), bottomRight(x + m_Square, y + m_Square);

    if (angle != 0.f) {
        const float cx = x + m_Square * 0.5f;
        const float cy = y + m_Square * 0.5f;

        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        const auto rotate = [&](sf::Vector2f& point) {
            const float dx = point.x - cx;
            const float dy = point.y - cy;

            point = sf::Vector2f(cx + dx * cosA - dy * sinA, cy + dx * sinA + dy * cosA);
        };

        rotate(topLeft);
        rotate(topRight);
        rotate(bottomLeft);
        rotate(bottomRight);
    }

    const sf::Vertex tl(topLeft, sf::Color::White, sf::Vector2f(tx, ty));
    const sf::Vertex tr(topRight, sf::Color::White, sf::Vector2f(tx + cellWidth, ty));
    const sf::Vertex bl(bottomLeft, sf::Color::White, sf::Vector2f(tx, ty + cellHeight));
    const sf::Vertex br(bottomRight, sf::Color::White, sf::Vector2f(tx + cellWidth, ty + cellHeight));

    out.push_back(tl);
    out.push_back(tr);
    out.push_back(bl);

    out.push_back(tr);
    out.push_back(br);
    out.push_back(bl);
}

void BoardView::renderPiece(sf::RenderTarget& target, Chess::Piece piece, float x, float y, float angle) const {
    static thread_local std::vector<sf::Vertex> single;
    single.clear();

    appendPiece(single, piece, x, y, angle);

    target.draw(single.data(), single.size(), sf::PrimitiveType::Triangles, &m_Pieces);
}

void BoardView::renderPieces(const UI::Painter& painter, bool mouseHeld) const {
    const Chess::Board& board = m_Game.Board();
    const uint64_t occupancy = board.GetOccupancyMap();

    // the dragged piece is drawn at the mouse instead
    const int dragged = mouseHeld ? m_Selected : Chess::NullPos;

    std::vector<sf::Vertex>& batch = m_PieceBatch;
    batch.clear();
    batch.reserve((Chess::NullPos + 4) * 6);

    for (int idx = 0; idx < Chess::NullPos; ++idx) {
        if (idx == dragged || !(occupancy & Chess::IndexToMask(idx))) continue;

        const bool animating = std::any_of(m_Animations.begin(), m_Animations.end(), [idx](const PieceMoveAnim& anim) {
            return anim.End == idx;
        });

        if (animating) continue;

        const sf::Vector2f position = squarePosition(idx);
        appendPiece(batch, board.GetPieceAt(idx), position.x, position.y);
    }

    for (const PieceMoveAnim& anim : m_Animations) {
        const sf::Vector2f from(
            static_cast<float>(mapFile(Chess::ToFile(anim.End))), static_cast<float>(mapRank(Chess::ToRank(anim.End)))
        );

        const sf::Vector2f to(
            static_cast<float>(mapFile(Chess::ToFile(anim.Start))), static_cast<float>(mapRank(Chess::ToRank(anim.Start)))
        );

        const sf::Vector2f direction = to - from;
        const sf::Vector2f position = (from + direction * anim.Timer) * m_Square;

        const float length = Length(direction);
        const float tilt = length ? -direction.x / length * 1.2f * anim.Timer * (1.f - anim.Timer) : 0.f;

        appendPiece(batch, board.GetPieceAt(anim.End), m_Origin.x + position.x, m_Origin.y + position.y, tilt);
    }

    if (!batch.empty()) painter.Target.draw(batch.data(), batch.size(), sf::PrimitiveType::Triangles, &m_Pieces);
}

void BoardView::renderHighlights(const UI::Painter& painter, sf::Vector2i mouse) const {
    sf::RenderTarget& target = painter.Target;
    const sf::Vector2f squareSize(m_Square, m_Square);

    const auto highlight = [&](uint8_t square, sf::Color color) {
        if (m_Markers & Chess::IndexToMask(square)) return;

        Utils::RenderQuad(target, color, squarePosition(square), squareSize);
    };

    for (uint64_t remaining = m_Markers; remaining; remaining &= remaining - 1) {
        m_Theme->RenderMarker(target, squarePosition(Chess::MaskToIndex(remaining)), m_Square);
    }

    if (m_IllegalTimer && static_cast<int>(m_IllegalTimer / 0.25f) % 2) {
        m_Theme->RenderMarker(target, squarePosition(m_IllegalSquare), m_Square);
    }

    const Chess::Move last = m_Game.LastMove();

    if (last) {
        highlight(last.StartingSquare, m_Theme->GetLastMoveHighlight());

        const bool targetIsLegalForSelection = std::any_of(m_LegalMoves.begin(), m_LegalMoves.end(), [&](Chess::Move move) {
            return move.TargetSquare == last.TargetSquare;
        });

        if (m_Theme->DrawLastMoveTargetIfCapturable() || !targetIsLegalForSelection) {
            highlight(last.TargetSquare, m_Theme->GetLastMoveHighlight());
        }
    }

    if (hasSelection()) {
        if (m_Theme->DrawSelectedPieceGhost()) {
            const sf::Vector2f position = squarePosition(m_Selected);
            renderPiece(target, m_Game.Board().GetPieceAt(m_Selected), position.x, position.y);
        }

        highlight(m_Selected, m_Theme->GetSelectedPiece());
    }

    const std::optional<int> hovered = squareAt(mouse);
    const uint64_t enemies = m_Game.Board().GetOccupancyMap(Chess::InvertColor(m_Game.SideToMove()));

    const float dotRadius = m_Square * 0.15f;
    const float dotOffset = m_Square * 0.5f - dotRadius;
    std::optional<sf::CircleShape> dot;

    for (const Chess::Move& move : m_LegalMoves) {
        const sf::Vector2f position = squarePosition(move.TargetSquare);

        if (m_Theme->DrawHoveredLegalSq() && hovered && move.TargetSquare == *hovered) {
            Utils::RenderQuad(target, m_Theme->GetLegalMoveHighlight(), position, squareSize);
        } else if (enemies & Chess::IndexToMask(move.TargetSquare)) {
            m_Theme->RenderCaptureOverlay(target, position, m_Square);
        } else {
            if (!dot) {
                dot.emplace(dotRadius);
                dot->setFillColor(m_Theme->GetLegalMoveHighlight());
            }

            dot->setPosition(sf::Vector2f(position.x + dotOffset, position.y + dotOffset));

            target.draw(*dot);
        }
    }

    if (hasSelection() && hovered && m_Theme->DrawHoveredSqOutline() && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
        Utils::RenderFrame(target, sf::Color(255, 255, 255, 166), squarePosition(*hovered), squareSize, 0.05f);
    }
}

void BoardView::renderArrow(sf::RenderTarget& target, Arrow arrow) const {
    const float arrowWidth = m_Square * 0.185f;
    const float headLength = m_Square * 0.40f;
    const float headWidth = m_Square * 0.45f;

    const sf::Vector2f half(m_Square * 0.5f, m_Square * 0.5f);
    const sf::Vector2f startPos = squarePosition(arrow.Start) + half;
    const sf::Vector2f endPos = squarePosition(arrow.End) + half;
    const sf::Color color = m_Theme->GetArrow();

    const int rankDiff = std::abs(Chess::ToRank(arrow.End) - Chess::ToRank(arrow.Start));
    const int fileDiff = std::abs(Chess::ToFile(arrow.End) - Chess::ToFile(arrow.Start));

    const bool knightMove = (rankDiff == 2 && fileDiff == 1) || (rankDiff == 1 && fileDiff == 2);

    if (m_Theme->DrawKnightArrowAsL() && knightMove) {
        const sf::Vector2f midPos = rankDiff == 2 ? sf::Vector2f(startPos.x, endPos.y) : sf::Vector2f(endPos.x, startPos.y);

        const sf::Vector2f dir1 = midPos - startPos;
        const sf::Vector2f dir2 = endPos - midPos;
        const float len1 = Length(dir1), len2 = Length(dir2);

        if (len1 <= 0.f || len2 <= 0.f) return;

        const sf::Vector2f norm1 = Normalized(dir1, len1);
        const sf::Vector2f perp1(-norm1.y, norm1.x);
        const sf::Vector2f norm2 = Normalized(dir2, len2);
        const sf::Vector2f perp2(-norm2.y, norm2.x);

        const sf::Vector2f seg1End = midPos + norm1 * (arrowWidth * 0.5f);
        const sf::Vector2f seg2Start = midPos + norm2 * (arrowWidth * 0.5f);
        const sf::Vector2f shaftEnd = endPos - norm2 * headLength;

        const sf::Vertex shaft1[] = {
            sf::Vertex(startPos + perp1 * (arrowWidth * 0.5f), color),
            sf::Vertex(startPos - perp1 * (arrowWidth * 0.5f), color),
            sf::Vertex(seg1End + perp1 * (arrowWidth * 0.5f), color),
            sf::Vertex(seg1End - perp1 * (arrowWidth * 0.5f), color)
        };

        const sf::Vertex shaft2[] = {
            sf::Vertex(seg2Start + perp2 * (arrowWidth * 0.5f), color),
            sf::Vertex(seg2Start - perp2 * (arrowWidth * 0.5f), color),
            sf::Vertex(shaftEnd + perp2 * (arrowWidth * 0.5f), color),
            sf::Vertex(shaftEnd - perp2 * (arrowWidth * 0.5f), color)
        };

        const sf::Vertex head[] = {
            sf::Vertex(shaftEnd + perp2 * (headWidth * 0.5f), color),
            sf::Vertex(endPos, color),
            sf::Vertex(shaftEnd - perp2 * (headWidth * 0.5f), color)
        };

        target.draw(shaft1, 4, sf::PrimitiveType::TriangleStrip);
        target.draw(shaft2, 4, sf::PrimitiveType::TriangleStrip);
        target.draw(head, 3, sf::PrimitiveType::Triangles);

        return;
    }

    const sf::Vector2f dir = endPos - startPos;
    const float length = Length(dir);

    if (length == 0.f) return;

    const sf::Vector2f norm = Normalized(dir, length);
    const sf::Vector2f perp(-norm.y, norm.x);

    const sf::Vector2f adjustedStart = startPos + norm * (m_Square * 0.15f);
    const sf::Vector2f adjustedEnd = endPos - norm * (m_Square * 0.15f);
    const sf::Vector2f shaftEnd = adjustedEnd - norm * headLength;

    const sf::Vertex shaft[] = {
        sf::Vertex(adjustedStart + perp * (arrowWidth * 0.5f), color),
        sf::Vertex(adjustedStart - perp * (arrowWidth * 0.5f), color),
        sf::Vertex(shaftEnd + perp * (arrowWidth * 0.5f), color),
        sf::Vertex(shaftEnd - perp * (arrowWidth * 0.5f), color)
    };

    const sf::Vertex head[] = {
        sf::Vertex(shaftEnd + perp * (headWidth * 0.5f), color),
        sf::Vertex(adjustedEnd, color),
        sf::Vertex(shaftEnd - perp * (headWidth * 0.5f), color)
    };

    target.draw(shaft, 4, sf::PrimitiveType::TriangleStrip);
    target.draw(head, 3, sf::PrimitiveType::Triangles);
}

void BoardView::renderPromotionMenu(const UI::Painter& painter) const {
    sf::RenderTarget& target = painter.Target;

    const int visualRank = mapRank(Chess::ToRank(m_PromotionTarget));
    const float menuX = squarePosition(m_PromotionTarget).x;
    const bool openDown = visualRank < Chess::Ranks / 2;

    const float boardLeft = m_Origin.x;
    const float boardRight = boardLeft + Chess::Files * m_Square;
    const float boardTop = m_Origin.y;
    const float boardBottom = boardTop + Chess::Ranks * m_Square;

    const float cardTop = boardTop + (openDown ? static_cast<float>(visualRank) : static_cast<float>(visualRank) - 3.f) * m_Square;
    const float cardBottom = cardTop + 4.f * m_Square;

    const sf::Color dim(0, 0, 0, 130);

    if (menuX > boardLeft) {
        Utils::RenderQuad(target, dim, {boardLeft, boardTop}, {menuX - boardLeft, boardBottom - boardTop});
    }

    if (menuX + m_Square < boardRight) {
        Utils::RenderQuad(target, dim, {menuX + m_Square, boardTop}, {boardRight - menuX - m_Square, boardBottom - boardTop});
    }

    if (cardTop > boardTop) Utils::RenderQuad(target, dim, {menuX, boardTop}, {m_Square, cardTop - boardTop});
    if (cardBottom < boardBottom) Utils::RenderQuad(target, dim, {menuX, cardBottom}, {m_Square, boardBottom - cardBottom});

    Utils::RenderRoundedQuad(target, sf::Color(255, 255, 255, 245), {menuX, cardTop}, {m_Square, 4.f * m_Square}, 0.12f);

    int i = 0;
    for (const Chess::PieceType type : Chess::PromotionTypes) {
        const float y = boardTop + static_cast<float>(openDown ? visualRank + i : visualRank - i) * m_Square;

        renderPiece(target, Chess::Piece{type, m_Game.SideToMove()}, menuX, y);
        ++i;
    }
}

void BoardView::renderGameOver(const UI::Painter& painter) const {
    sf::RenderTarget& target = painter.Target;
    const UI::Context& ctx = painter.Ctx;

    const float eased = Smooth(std::min(1.f, m_GameOverTimer / GameOverFadeIn));
    const float boardSize = Chess::Files * m_Square;

    Utils::RenderQuad(target, Utils::ConvertAlpha(ctx.Pal.Scrim, eased), m_Origin, {boardSize, boardSize});

    m_Particles.Render(target, m_Theme->GetParticles(), eased);

    const unsigned int titleSize = static_cast<unsigned int>(m_Square * 0.55f);
    const unsigned int subSize = static_cast<unsigned int>(m_Square * 0.22f);

    const float titleWidth = ctx.TextWidth(m_GameOverTitle, titleSize, true);
    const float subWidth = ctx.TextWidth(m_GameOverSubtitle, subSize);
    const float titleHeight = ctx.CapMetrics(titleSize, true).second;
    const float subHeight = ctx.CapMetrics(subSize, false).second;

    const float padH = m_Square * 0.60f;
    const float padV = m_Square * 0.50f;
    const float gap = m_Square * 0.26f;

    const float cardWidth = std::max(titleWidth, subWidth) + padH * 2.f;
    const float cardHeight = titleHeight + subHeight + gap + padV * 2.f;

    const sf::Vector2f center = m_Origin + sf::Vector2f(boardSize, boardSize) * 0.5f;
    const float slide = (1.f - eased) * m_Square * 0.55f;
    const sf::Vector2f cardPosition(center.x - cardWidth * 0.5f, center.y - cardHeight * 0.5f + slide);

    Utils::RenderRoundedQuad(target, Utils::ConvertAlpha(ctx.Pal.Panel, eased), cardPosition, {cardWidth, cardHeight}, 0.1f);

    // separator in the accent colour
    const float lineWidth = cardWidth * 0.5f;
    const float lineY = cardPosition.y + padV + titleHeight + gap * 0.5f;

    Utils::RenderQuad(target, Utils::ConvertAlpha(ctx.Pal.Accent, eased), {center.x - lineWidth * 0.5f, lineY}, {lineWidth, 2.f});

    painter.Text(
        m_GameOverTitle, sf::FloatRect({cardPosition.x, cardPosition.y + padV}, {cardWidth, titleHeight}),
        titleSize, Utils::ConvertAlpha(ctx.Pal.Text, eased), UI::Align::Center, true
    );

    painter.Text(
        m_GameOverSubtitle, sf::FloatRect({cardPosition.x, cardPosition.y + padV + titleHeight + gap}, {cardWidth, subHeight}),
        subSize, Utils::ConvertAlpha(ctx.Pal.TextDim, eased), UI::Align::Center
    );
}

void BoardView::Render(const UI::Painter& painter, sf::Vector2i mouse) const {
    if (!m_Theme) return;

    sf::RenderTarget& target = painter.Target;
    const bool mouseHeld = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    const float boardSize = Chess::Files * m_Square;

    renderBoard(painter);

    if (m_InspectionT > 0.f) {
        Utils::RenderQuad(
            target, sf::Color(255, 245, 255, static_cast<uint8_t>(38 * m_InspectionT)), m_Origin, {boardSize, boardSize}
        );
    }

    renderHighlights(painter, mouse);
    renderPieces(painter, mouseHeld);

    if (m_DrawingArrow) renderArrow(target, m_DrawingArrow);

    for (const Arrow arrow : m_Arrows) renderArrow(target, arrow);

    if (mouseHeld && hasSelection()) {
        renderPiece(
            target, m_Game.Board().GetPieceAt(m_Selected),
            static_cast<float>(mouse.x) - m_Square * 0.5f, static_cast<float>(mouse.y) - m_Square * 0.5f, m_DragTilt
        );
    }

    if (m_GameOverTimer > 0.f) renderGameOver(painter);

    if (m_PromotionActive) renderPromotionMenu(painter);
}
