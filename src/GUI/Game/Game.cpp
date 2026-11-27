#include "GUI/Game/Game.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string_view>

void Game::loadPosition(const std::string& fen) {
    m_Board.NewGame();

    m_SideToMove = m_Board.LoadFromFen(fen);
    m_Board.SetEngineColor(m_SideToMove);
    m_Board.ClearHash();

    m_LastMove = Chess::Move();
    m_DrawOfferedBy.reset();
}

void Game::updateResult() {
    m_Result = GameResult::None;
    m_Winner.reset();

    if (!m_Board.HasAnyLegalMoves(m_SideToMove)) {
        if (m_Board.isUnderCheck(m_SideToMove)) {
            m_Result = GameResult::Checkmate;
            m_Winner = Chess::InvertColor(m_SideToMove);
        } else {
            m_Result = GameResult::Stalemate;
        }
    } else if (m_Board.FiftyMoveRule()) {
        m_Result = GameResult::DrawFiftyMove;
    } else if (m_Board.HasInsufficientMaterial()) {
        m_Result = GameResult::DrawInsufficient;
    } else if (m_Board.isThreefoldRepetition()) {
        m_Result = GameResult::DrawRepetition;
    }
}

bool Game::Reset(const std::string& fen) {
    const std::optional<Fen::Parsed> parsed = Fen::Parse(fen);

    if (!parsed) return false;

    m_Inspection.reset();

    loadPosition(parsed->Text);

    m_History.clear();
    m_FirstSide = m_SideToMove;
    m_FirstMoveNumber = parsed->FullMove;

    updateResult();

    return true;
}

void Game::DoMove(Chess::Move move) {
    std::string san = Notation::ToSan(m_Board, move);

    m_Board.DoMove(move);
    m_SideToMove = Chess::InvertColor(m_SideToMove);
    m_LastMove = move;
    m_DrawOfferedBy.reset();

    updateResult();

    if (m_Result == GameResult::Checkmate) san += '#';
    else if (m_Board.isUnderCheck(m_SideToMove)) san += '+';

    m_History.push_back({move, std::move(san)});
}

void Game::FlipSideToMove() {
    m_SideToMove = Chess::InvertColor(m_SideToMove);
    m_Board.FlipSideToMove();

    m_History.clear();
    m_FirstSide = m_SideToMove;
    m_LastMove = Chess::Move();
    m_DrawOfferedBy.reset();

    updateResult();
}

void Game::Resign(Chess::PieceColor loser) {
    m_Result = GameResult::Resignation;
    m_Winner = Chess::InvertColor(loser);
    m_DrawOfferedBy.reset();
}

void Game::Abandon(Chess::PieceColor loser) {
    m_Result = GameResult::Abandoned;
    m_Winner = Chess::InvertColor(loser);
    m_DrawOfferedBy.reset();
}

void Game::OfferDraw(Chess::PieceColor by) {
    if (!Over()) m_DrawOfferedBy = by;
}

void Game::AcceptDraw() {
    m_Result = GameResult::DrawAgreement;
    m_Winner.reset();
    m_DrawOfferedBy.reset();
}

void Game::BeginInspection() {
    if (m_Inspection) return;

    m_Inspection = Snapshot{
        m_Board.GetFen(m_SideToMove), m_LastMove, m_History, m_FirstSide, m_FirstMoveNumber
    };
}

void Game::EndInspection() {
    if (!m_Inspection) return;

    const Snapshot snapshot = std::move(*m_Inspection);
    m_Inspection.reset();

    loadPosition(snapshot.Fen);

    m_LastMove = snapshot.LastMove;
    m_History = snapshot.History;
    m_FirstSide = snapshot.FirstSide;
    m_FirstMoveNumber = snapshot.FirstMoveNumber;

    updateResult();
}

#pragma region Fen

namespace {
    bool IsNumber(const std::string& s) {
        return !s.empty() && s.size() <= 5 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
    }

    bool ValidPlacement(const std::string& placement) {
        int rank = 0, file = 0, whiteKings = 0, blackKings = 0;

        for (std::size_t i = 0; i <= placement.size(); ++i) {
            const char c = i < placement.size() ? placement[i] : '/';

            if (c == '/') {
                if (file != 8) return false;
                file = 0;
                if (++rank > 8) return false;
            } else if (c >= '1' && c <= '8') {
                file += c - '0';
                if (file > 8) return false;
            } else if (std::string_view("pnbrqkPNBRQK").find(c) != std::string_view::npos) {
                if ((c == 'p' || c == 'P') && (rank == 0 || rank == 7)) return false;
                if (c == 'K') ++whiteKings;
                if (c == 'k') ++blackKings;
                if (++file > 8) return false;
            } else {
                return false;
            }
        }

        return rank == 8 && whiteKings == 1 && blackKings == 1;
    }
}

std::optional<Fen::Parsed> Fen::Parse(const std::string& text) {
    std::istringstream stream(text);
    std::vector<std::string> fields;

    for (std::string token; stream >> token;) fields.push_back(token);

    if (fields.size() < 2 || fields.size() > 6) return std::nullopt;

    constexpr std::array<const char*, 4> Defaults = {"-", "-", "0", "1"};
    while (fields.size() < 6) fields.push_back(Defaults[fields.size() - 2]);

    if (!ValidPlacement(fields[0])) return std::nullopt;
    if (fields[1] != "w" && fields[1] != "b") return std::nullopt;

    if (fields[2] != "-") {
        for (char c : fields[2]) {
            if (std::string_view("KQkq").find(c) == std::string_view::npos) return std::nullopt;
        }
    }

    if (fields[3] != "-") {
        const std::string& ep = fields[3];
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' || (ep[1] != '3' && ep[1] != '6')) return std::nullopt;
    }

    if (!IsNumber(fields[4]) || !IsNumber(fields[5])) return std::nullopt;

    Parsed result;
    result.BlackToMove = fields[1] == "b";
    result.FullMove = static_cast<unsigned int>(std::max(1, std::stoi(fields[5])));
    fields[5] = std::to_string(result.FullMove);

    for (const std::string& field : fields) {
        if (!result.Text.empty()) result.Text += ' ';
        result.Text += field;
    }

    return result;
}

bool Fen::Plausible(const std::string& text) {
    const std::optional<Parsed> parsed = Parse(text);

    if (!parsed) return false;

    std::istringstream stream(parsed->Text);
    std::string placement, side, castling, enPassant;
    stream >> placement >> side >> castling >> enPassant;

    char board[64] = {};
    int square = 0;

    for (const char c : placement) {
        if (c == '/') continue;
        if (c >= '1' && c <= '8') square += c - '0';
        else board[square++] = c;
    }

    for (const char right : castling) {
        const bool ok = right == 'K' ? board[60] == 'K' && board[63] == 'R'
                      : right == 'Q' ? board[60] == 'K' && board[56] == 'R'
                      : right == 'k' ? board[4] == 'k' && board[7] == 'r'
                      : right == 'q' ? board[4] == 'k' && board[0] == 'r'
                      : true; // "-"

        if (!ok) return false;
    }

    if (enPassant != "-") {
        const int file = enPassant[0] - 'a';
        const bool whiteToMove = side == "w";

        if (whiteToMove != (enPassant[1] == '6')) return false;

        const int target = (whiteToMove ? 2 : 5) * 8 + file;
        const int pawn = target + (whiteToMove ? 8 : -8);
        const int origin = target + (whiteToMove ? -8 : 8);

        if (board[target] || board[origin] || board[pawn] != (whiteToMove ? 'p' : 'P')) return false;
    }

    return true;
}

#pragma region Notation

std::string Notation::ToSan(const Chess::Board& board, Chess::Move move) {
    using namespace Chess;

    if (isCastle(move.Flag)) {
        return HasFlag(move.Flag, MoveFlag::CastleKingSide) ? "O-O" : "O-O-O";
    }

    const auto fileChar = [](int idx) -> char { return static_cast<char>('a' + ToFile(idx)); };
    const auto rankChar = [](int idx) -> char { return static_cast<char>('1' + (Ranks - 1 - ToRank(idx))); };

    const Piece piece = board.GetPieceAt(move.StartingSquare);
    const bool capture = HasFlag(move.Flag, MoveFlag::Capture) || HasFlag(move.Flag, MoveFlag::EnPassant);

    std::string san;

    if (piece.Type == PieceType::Pawn) {
        if (capture) san += fileChar(move.StartingSquare);
    } else {
        san += static_cast<char>(std::toupper(PieceToChar(piece)));

        bool ambiguous = false, sameFile = false, sameRank = false;
        const uint64_t occupancy = board.GetOccupancyMap(piece.Color);

        for (int sq = 0; sq < NullPos; ++sq) {
            if (sq == move.StartingSquare || !(occupancy & IndexToMask(sq))) continue;

            const Piece other = board.GetPieceAt(sq);
            if (other.Type != piece.Type) continue;

            for (const Move& m : board.GetLegalMoves(sq)) {
                if (m.TargetSquare != move.TargetSquare) continue;

                ambiguous = true;
                sameFile |= ToFile(sq) == ToFile(move.StartingSquare);
                sameRank |= ToRank(sq) == ToRank(move.StartingSquare);
                break;
            }
        }

        if (ambiguous) {
            if (!sameFile) san += fileChar(move.StartingSquare);
            else if (!sameRank) san += rankChar(move.StartingSquare);
            else san += std::string{fileChar(move.StartingSquare), rankChar(move.StartingSquare)};
        }
    }

    if (capture) san += 'x';

    san += fileChar(move.TargetSquare);
    san += rankChar(move.TargetSquare);

    if (isPromotion(move.Flag)) {
        san += '=';
        if (HasFlag(move.Flag, MoveFlag::PromoteToQueen)) san += 'Q';
        else if (HasFlag(move.Flag, MoveFlag::PromoteToRook)) san += 'R';
        else if (HasFlag(move.Flag, MoveFlag::PromoteToKnight)) san += 'N';
        else san += 'B';
    }

    return san;
}
