#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Engine/Chess.hpp"

namespace Fen {
    struct Parsed {
        std::string Text; // normalised
        bool BlackToMove;
        unsigned int FullMove;
    };

    [[nodiscard]]
    std::optional<Parsed> Parse(const std::string& text);
}

namespace Notation {
    [[nodiscard]]
    std::string ToSan(const Chess::Board& board, Chess::Move move);
}

enum class GameResult : uint8_t {
    None,
    Checkmate,
    Stalemate,
    DrawFiftyMove,
    DrawRepetition,
    DrawInsufficient,
    DrawAgreement,
    Resignation
};

class Game final {
public:
    struct Entry {
        Chess::Move Move;
        std::string San;
    };

private:
    struct Snapshot {
        std::string Fen;
        Chess::Move LastMove;
        std::vector<Entry> History;
        Chess::PieceColor FirstSide;
        unsigned int FirstMoveNumber;
    };

    void loadPosition(const std::string& fen);
    void updateResult();

    Chess::Board m_Board;
    Chess::PieceColor m_SideToMove{Chess::PieceColor::White};
    Chess::Move m_LastMove;

    std::vector<Entry> m_History;
    Chess::PieceColor m_FirstSide{Chess::PieceColor::White};
    unsigned int m_FirstMoveNumber{1};

    GameResult m_Result{GameResult::None};
    std::optional<Chess::PieceColor> m_Winner;
    std::optional<Chess::PieceColor> m_DrawOfferedBy;

    std::optional<Snapshot> m_Inspection;

public:
    [[nodiscard]]
    bool Reset(const std::string& fen = Chess::DefaultFEN);

    void DoMove(Chess::Move move);
    void FlipSideToMove();

    void Resign(Chess::PieceColor loser);
    void OfferDraw(Chess::PieceColor by);
    void AcceptDraw();

    inline void DeclineDraw() noexcept {
        m_DrawOfferedBy.reset();
    }

    void BeginInspection();
    void EndInspection();

    [[nodiscard]]
    inline const Chess::Board& Board() const noexcept {
        return m_Board;
    }

    [[nodiscard]]
    inline Chess::Board& Board() noexcept {
        return m_Board;
    }

    [[nodiscard]]
    inline Chess::PieceColor SideToMove() const noexcept {
        return m_SideToMove;
    }

    [[nodiscard]]
    inline Chess::Move LastMove() const noexcept {
        return m_LastMove;
    }

    [[nodiscard]]
    inline const std::vector<Entry>& History() const noexcept {
        return m_History;
    }

    [[nodiscard]]
    inline Chess::PieceColor FirstSide() const noexcept {
        return m_FirstSide;
    }

    [[nodiscard]]
    inline unsigned int FirstMoveNumber() const noexcept {
        return m_FirstMoveNumber;
    }

    [[nodiscard]]
    inline GameResult Result() const noexcept {
        return m_Result;
    }

    [[nodiscard]]
    inline bool Over() const noexcept {
        return m_Result != GameResult::None;
    }

    [[nodiscard]]
    inline std::optional<Chess::PieceColor> Winner() const noexcept {
        return m_Winner;
    }

    [[nodiscard]]
    inline std::optional<Chess::PieceColor> DrawOfferedBy() const noexcept {
        return m_DrawOfferedBy;
    }

    [[nodiscard]]
    inline bool Inspecting() const noexcept {
        return m_Inspection.has_value();
    }
};
