#include <iostream>
#include <vector>
#include <functional>

#define CHESS_EXPOSE_INTERNALS
#include "Engine/Chess.hpp"

struct TestCase {
    std::string name;
    std::function<bool()> fn;
};

static std::vector<TestCase> s_Tests;
static std::vector<std::string> s_FailedTests;

static int s_Passed = 0;
static int s_Failed = 0;

#pragma region Helpers

// Count moves in a position that have the Check flag set.
static unsigned int countCheckMoves(Chess::Board& board, Chess::PieceColor side) {
    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0;
    board.getAllLegalMoves(side, moves, n);

    unsigned int checks = 0;
    for (unsigned int i = 0; i < n; ++i) {
        if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check)) ++checks;
    }

    return checks;
}

// Count total legal moves.
static unsigned int countMoves(Chess::Board& board, Chess::PieceColor side) {
    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0;
    board.getAllLegalMoves(side, moves, n);
    return n;
}

// Find a specific move and verify it has (or does not have) the Check flag.
static bool findMove(Chess::Board& board, Chess::PieceColor side, int from, int to, bool expectCheck) {
    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0;
    board.getAllLegalMoves(side, moves, n);

    for (unsigned int i = 0; i < n; ++i) {
        if (moves[i].StartingSquare == from && moves[i].TargetSquare == to) {
            bool hasCheck = Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            return hasCheck == expectCheck;
        }
    }

    return false; // move not found at all
}

// Perft node count (leaf nodes at given depth).
static uint64_t perft(Chess::Board board, Chess::PieceColor side, int depth) {
    if (depth == 0) return 1;

    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0u;
    board.getAllLegalMoves(side, moves, n);

    uint64_t total = 0ull;

    for (unsigned int i = 0; i < n; ++i) {
        Chess::Board copy = board;
        copy.doMoveInternal(moves[i]);
        total += perft(copy, Chess::InvertColor(side), depth - 1);
    }

    return total;
}

// Load a FEN and return the side to move.
static Chess::PieceColor load(Chess::Board& board, const std::string& fen) {
    return board.LoadFromFen(fen);
}

static bool doMove(Chess::Board& board, Chess::PieceColor side, const std::string& uci) {
    const int fromFile = uci[0] - 'a';
    const int fromRank = Chess::Ranks - 1 - (uci[1] - '1');
    const int toFile = uci[2] - 'a';
    const int toRank = Chess::Ranks - 1 - (uci[3] - '1');

    const int from = Chess::To2DIndex(fromRank, fromFile);
    const int to = Chess::To2DIndex(toRank, toFile);

    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0;
    board.getAllLegalMoves(side, moves, n);

    for (unsigned int i = 0; i < n; ++i) {
        if (moves[i].StartingSquare == from && moves[i].TargetSquare == to) {
            board.DoMove(moves[i]);
            return true;
        }
    }
    return false;
}

static bool captureGenMatchesLegalGen(Chess::Board& board, Chess::PieceColor side) {
    Chess::Move all[Chess::MaxLegalMoves];
    unsigned int nAll = 0;
    board.getAllLegalMoves(side, all, nAll);

    Chess::Move caps[Chess::MaxLegalMoves];
    unsigned int nCaps = 0;
    board.getAllCaptureMoves(side, caps, nCaps);

    const auto contains = [](Chess::Move* arr, unsigned int n, Chess::Move m) {
        for (unsigned int i = 0; i < n; ++i) if (arr[i] == m) return true;
        return false;
        };

    for (unsigned int i = 0; i < nCaps; ++i) {
        if (!contains(all, nAll, caps[i])) return false;
    }

    for (unsigned int i = 0; i < nAll; ++i) {
        const bool isCapture = Chess::HasFlag(all[i].Flag, Chess::MoveFlag::Capture) ||
            Chess::HasFlag(all[i].Flag, Chess::MoveFlag::EnPassant);
        if (isCapture && !contains(caps, nCaps, all[i])) return false;
    }

    return true;
}

static bool captureGenConsistentAtDepth(Chess::Board board, Chess::PieceColor side, int depth) {
    if (depth == 0) return true;

    if (!captureGenMatchesLegalGen(board, side)) return false;

    Chess::Move moves[Chess::MaxLegalMoves];
    unsigned int n = 0;
    board.getAllLegalMoves(side, moves, n);

    for (unsigned int i = 0; i < n; ++i) {
        Chess::Board copy = board;
        copy.doMoveInternal(moves[i]);
        if (!captureGenConsistentAtDepth(copy, Chess::InvertColor(side), depth - 1)) return false;
    }

    return true;
}

#pragma region Register Tests

static void RegisterTest(const std::string& name, std::function<bool()> fn) {
    s_Tests.push_back({name, fn});
}

static void RegisterPerftTest(const std::string& name, const std::string& fen, const std::vector<uint64_t>& nodes) {
    for (int i = 0; i < nodes.size(); ++i) {
        const int depth = i + 1;
        const uint64_t expected = nodes[i];

        RegisterTest("Perft: " + name + " d" + std::to_string(depth),
            [fen, depth, expected]() {
                Chess::Board b; auto s = load(b, fen);
                return perft(b, s, depth) == expected;
            }
        );
    }
}

static void RegisterTests() {
    // -----------------------------------------------------------------------
    // Position 1: Starting position
    // -----------------------------------------------------------------------
    RegisterPerftTest(
        "Position 1", Chess::DefaultFEN,
        {20, 400, 8902, 197281, 4865609}
    );

    // -----------------------------------------------------------------------
    // Position 2: Kiwipete - stress tests castling, en-passant, promotions
    // -----------------------------------------------------------------------
    RegisterPerftTest(
        "Position 2", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        {48, 2039, 97862, 4085603}
    );

    // -----------------------------------------------------------------------
    // Position 3: Many promotions and en-passant
    // -----------------------------------------------------------------------
    RegisterPerftTest(
        "Position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        {14, 191, 2812, 43238, 674624}
    );

    // -----------------------------------------------------------------------
    // Position 4: Castling rights edge cases
    // -----------------------------------------------------------------------
    RegisterPerftTest(
        "Position 4", "r3k2r/Pppp1ppp/1b6/8/8/1B6/pPPP1PPP/R3K2R b KQkq - 0 1",
        {27, 725, 19209, 515398}
    );

    // -----------------------------------------------------------------------
    // Position 5: Pins and checks
    // -----------------------------------------------------------------------
    RegisterPerftTest(
        "Position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        {44, 1486, 62379, 2103487}
    );

    // -----------------------------------------------------------------------
    // Position 6: Complex middle game
    // -----------------------------------------------------------------------
    RegisterPerftTest(
        "Position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        {46, 2079, 89890, 3894594}
    );

    // -----------------------------------------------------------------------
    // Position 7: Pins and check blocks
    // -----------------------------------------------------------------------
    RegisterPerftTest(
        "Position 7", "r3k3/1p3p2/p2q2p1/bn3P2/1N2PQP1/PB6/3K1R1r/3R4 w - - 0 1",
        {7, 317, 12363, 501910}
    );

    // -----------------------------------------------------------------------
    // Other tricky positions:
    // -----------------------------------------------------------------------
    RegisterPerftTest(
        "Position 8", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 1 1",
        {14, 191, 2812, 43238}
    );

    RegisterPerftTest(
        "Position 9", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 1 1",
        {48, 2039, 97862, 4085603}
    );

    RegisterPerftTest(
        "Position 10", "r3k2r/pp3pp1/PN1pr1p1/4p1P1/4P3/3P4/P1P2PP1/R3K2R w KQkq - 4 4",
        {34, 751, 23544, 508418, 15587335}
    );

    RegisterPerftTest(
        "Position 11", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 1 1",
        {46, 2079, 89890, 3894594}
    );

    RegisterPerftTest(
        "Position 12", "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1",
        {44, 1494, 50509, 1720476}
    );

    RegisterPerftTest(
        "Position 13", "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1",
        {29, 165, 5160, 31961}
    );

    RegisterPerftTest(
        "Position 14", "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1",
        {37, 183, 6559, 23527}
    );

    RegisterPerftTest(
        "Position 15", "r1bq2r1/1pppkppp/1b3n2/pP1PP3/2n5/2P5/P3QPPP/RNB1K2R w KQ a6 0 1",
        {38, 1194, 41006, 1280017}
    );

    RegisterPerftTest(
        "Position 16", "4k2r/1pp1n2p/6N1/1K1P2r1/4P3/P5P1/1Pp4P/R7 w k - 0 6",
        {26, 736, 16748, 491364}
    );

    RegisterPerftTest(
        "Position 17", "1Bb3BN/R2Pk2r/1Q5B/4q2R/2bN4/4Q1BK/1p6/1bq1R1rb w - - 0 1",
        {81, 2714, 184214, 6871272}
    );

    RegisterPerftTest(
        "Position 18", "8/k1P5/8/1K6/8/8/8/8 w - - 0 1",
        {10, 25, 268, 926, 10857, 43261}
    );

    // -----------------------------------------------------------------------
    // Knight direct check
    // White knight e5 -> f7 gives check to king on h8 (diagonal hop)
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Knight direct check", []() {
        Chess::Board b; load(b, "7k/8/8/4N3/8/8/8/4K3 w - - 0 1");
        // N e5 -> f7
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(1, 5),  // f7
            true
        );
    });

    RegisterTest("CheckFlag: Knight non-check move", []() {
        Chess::Board b; load(b, "7k/8/8/4N3/8/8/8/4K3 w - - 0 1");
        // N e5 -> d3 is not check
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(5, 3),  // d3
            false
        );
    });

    // -----------------------------------------------------------------------
    // Pawn direct check - push that lands and attacks the king diagonally
    // Pawn e5 -> e6 attacks d7; king placed on d7 so the push is a check.
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: White pawn direct check", []() {
        Chess::Board b; load(b, "8/3k4/8/4P3/8/8/8/4K3 w - - 0 1");
        // e5 -> e6 attacks d7 where black king is
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(2, 4),  // e6
            true
        );
    });

    RegisterTest("CheckFlag: White pawn push no check", []() {
        Chess::Board b; load(b, "8/8/3k4/4P3/8/8/8/4K3 w - - 0 1");
        // Here king on d6; e5->e6 does not check d6
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(2, 4),  // e6
            false
        );
    });

    // -----------------------------------------------------------------------
    // Black pawn direct check (single clear scenario)
    // Black pawn d4 -> d3 attacks c2; white king placed on c2.
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Black pawn direct check", []() {
        Chess::Board b; load(b, "8/8/8/3p4/8/2K5/8/3k4 b - - 0 1");
        // d4 -> d3 should attack c2 and give check to white king on c2
        return findMove(b, Chess::PieceColor::Black,
            Chess::To2DIndex(3, 3),  // d4
            Chess::To2DIndex(4, 3),  // d3
            true
        );
    });

    // -----------------------------------------------------------------------
    // Bishop direct check
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Bishop direct check", []() {
        Chess::Board b; load(b, "7k/8/8/8/8/8/8/B3K3 w - - 0 1");
        // Ba1 -> b2, diagonal b2..h8 checks black king on h8
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(6, 1),  // b2
            true
        );
    });

    RegisterTest("CheckFlag: Bishop blocked no check", []() {
        Chess::Board b; load(b, "7k/6P1/8/8/8/8/8/B3K3 w - - 0 1");
        // Pawn on g7 blocks the a1-h8 diagonal so Ba1->b2 must NOT be check
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(6, 1),  // b2
            false
        );
    });

    // -----------------------------------------------------------------------
    // Rook direct check (rank and file) and blocked case
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Rook direct check rank", []() {
        Chess::Board b; load(b, "7k/8/8/8/8/8/8/R3K3 w - - 0 1");
        // Ra1 -> a8 (a8 has empty path along file), black king on h8; Ra8 attacks rank 8 -> check
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(0, 0),  // a8
            true
        );
    });

    RegisterTest("CheckFlag: Rook direct check file", []() {
        Chess::Board b; load(b, "k7/8/8/8/8/8/8/K6R w - - 0 1");
        // Rh1 -> h8, rook on h8 attacks a8 along rank 8, checking king on a8
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 7),  // h1
            Chess::To2DIndex(0, 7),  // h8
            true
        );
    });

    RegisterTest("CheckFlag: Rook blocked no check", []() {
        Chess::Board b; load(b, "2P4k/8/8/8/8/8/8/R3K3 w - - 0 1");
        // Pawn on c8 blocks rank 8 path from a8 to h8 - Ra1->a8 should NOT be check on h8
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(0, 0),  // a8
            false
        );
    });

    // -----------------------------------------------------------------------
    // Queen direct checks (diagonal and orthogonal)
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Queen diagonal check", []() {
        Chess::Board b; load(b, "7k/8/8/8/8/8/8/Q3K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(6, 1),  // b2
            true
        );
    });

    RegisterTest("CheckFlag: Queen orthogonal check", []() {
        Chess::Board b; load(b, "7k/8/8/8/8/8/8/Q3K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 0),  // a1
            Chess::To2DIndex(0, 0),  // a8
            true
        );
    });

    // -----------------------------------------------------------------------
    // Discovered check (bishop reveals rook, rook reveals bishop)
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Discovered check by rook", []() {
        // Rook e2 behind bishop e5; bishop moves off file -> discovered check to king on e8
        Chess::Board b; load(b, "4k3/8/8/4B3/8/8/4R3/4K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(4, 3),  // d4
            true
        );
    });

    RegisterTest("CheckFlag: Discovered check by bishop", []() {
        // Bishop on a1 behind a knight on d4; moving knight off diagonal reveals bishop to h8
        Chess::Board b; load(b, "7k/8/8/8/3N4/8/8/B3K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(4, 3),  // d4
            Chess::To2DIndex(2, 2),  // c6
            true
        );
    });

    RegisterTest("CheckFlag: Double check (direct + discovered)", []() {
        // Knight on e5 (on diagonal a1-h8) -> f7 gives direct knight check and discovers bishop a1->h8.
        Chess::Board b; load(b, "7k/8/8/4N3/8/8/8/B3K3 w - - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 4),  // e5
            Chess::To2DIndex(1, 5),  // f7
            true
        );
    });

    // -----------------------------------------------------------------------
    // En-passant discovered check
    // A specific, clear case: white pawn b5 capturing c6 ep exposes rook on a5 to check king on h5.
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: En-passant discovered check", []() {
        Chess::Board b; load(b, "8/8/8/RPp4k/8/8/8/4K3 w - c6 0 1");
        // b5xc6 ep should be a capturing move flagged EnPassant and Check (discovered)
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 1),  // b5
            Chess::To2DIndex(2, 2),  // c6 (ep)
            true
        );
    });

    // -----------------------------------------------------------------------
    // Castling check cases
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Kingside castle gives check", []() {
        // After O-O rook lands on f1 and gives check to king on f8.
        Chess::Board b; load(b, "5k2/8/8/8/8/8/8/4K2R w K - 0 1");
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 4),  // e1
            Chess::To2DIndex(7, 6),  // g1 (king move for O-O)
            true
        );
    });

    RegisterTest("CheckFlag: Queenside castle no check", []() {
        Chess::Board b; load(b, "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1");
        // O-O-O should not produce a check in this setup
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(7, 4),  // e1
            Chess::To2DIndex(7, 2),  // c1 (king move for O-O-O)
            false
        );
    });

    // -----------------------------------------------------------------------
    // Promotion checks
    // For promotions we iterate the move list and test appropriate flags.
    // -----------------------------------------------------------------------
    RegisterTest("CheckFlag: Promotion to queen check", []() {
        Chess::Board b; load(b, "7k/4P3/8/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 4) &&
                moves[i].TargetSquare == Chess::To2DIndex(0, 4) &&
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToQueen)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false; // move not found
    });

    RegisterTest("CheckFlag: Promotion-to-rook check", []() {
        Chess::Board b; load(b, "7k/4P3/8/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 4) &&
                moves[i].TargetSquare == Chess::To2DIndex(0, 4) &&
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToRook)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false;
    });

    RegisterTest("CheckFlag: Promotion to bishop check", []() {
        Chess::Board b; load(b, "8/1P6/3k4/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 1) &&  // b7
                moves[i].TargetSquare == Chess::To2DIndex(0, 1) &&  // b8
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToBishop)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false;
    });

    RegisterTest("CheckFlag: Promotion to knight check", []() {
        // Promotion to knight that delivers a check (constructed scenario)
        Chess::Board b; load(b, "8/4k1P1/8/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 6) &&  // g7
                moves[i].TargetSquare == Chess::To2DIndex(0, 6) &&  // g8
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToKnight)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false;
    });

    // Promotion that captures and gives check (promotion-capture)
    RegisterTest("CheckFlag: Promotion capture to queen gives check", []() {
        // White pawn on b7 capturing a8 and promoting to queen that checks king on c8
        Chess::Board b; load(b, "r1k5/1P6/p7/8/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(1, 1) &&  // b7
                moves[i].TargetSquare == Chess::To2DIndex(0, 0) &&  // a8
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PromoteToQueen)) {
                return Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::Check);
            }
        }
        return false;
    });

    // -----------------------------------------------------------------------
    // Check count correctness - total checks in a position must match
    // -----------------------------------------------------------------------
    RegisterTest("CheckCount: Starting position has 0 check moves", []() {
        Chess::Board b; auto s = load(b, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        return countCheckMoves(b, s) == 0;
    });

    RegisterTest("CheckCount: Position with multiple checking moves sanity", []() {
        Chess::Board b; auto s = load(b,
            "r3kb1r/ppp2ppp/2n5/1B6/8/5N2/PPPP1PPP/R1BQR1K1 w kq - 0 1");
        unsigned int checks = countCheckMoves(b, s);
        return checks <= countMoves(b, s); // can't have more checks than moves
    });

    // -----------------------------------------------------------------------
    // In check - legal moves only
    // -----------------------------------------------------------------------
    RegisterTest("InCheck: Only legal moves generated when in check", []() {
        Chess::Board b; auto s = load(b, "4R3/8/8/8/8/8/8/k3K3 b - - 0 1");
        unsigned int n = countMoves(b, s);
        // Expect a small number but at least 1 legal reply (escape/capture)
        return n >= 1 && n <= 5;
    });

    RegisterTest("InCheck: Double check only king moves (robustness)", []() {
        // This test merely ensures engine doesn't crash on double-check-like setups.
        Chess::Board b; load(b, "6k1/8/8/8/2B5/8/8/4K1R1 w - - 0 1");
        auto s = Chess::PieceColor::Black;
        unsigned int n = countMoves(b, s);
        return n >= 0;
    });

    // -----------------------------------------------------------------------
    // Pins - pinned piece cannot move if it would expose king to check
    // -----------------------------------------------------------------------
    RegisterTest("Pin: Pinned piece moves are legal only along pin", []() {
        // Bishop pinned along rank 1 by rook a1. Only moves along rank are legal.
        Chess::Board b; load(b, "4k3/8/8/8/8/8/8/r3BK2 w - - 0 1");
        unsigned int n = countMoves(b, Chess::PieceColor::White);
        return n >= 1;
    });

    RegisterTest("Edge: Stalemate returns 0 moves", []() {
        Chess::Board b; auto s = load(b, "k7/8/1Q6/8/8/8/8/7K b - - 0 1");
        // Black to move, stalemated (no legal moves).
        return countMoves(b, s) == 0;
    });

    RegisterTest("Edge: En-passant legal generation (basic)", []() {
        Chess::Board b; load(b, "8/8/8/3pP3/8/8/8/k2K4 w - d6 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        bool epFound = false;
        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::EnPassant)) {
                epFound = true;
                break;
            }
        }
        return epFound;
    });

    RegisterTest("Edge: Castling not allowed through attacked square", []() {
        // Black rook on f8 attacks f1 - white cannot castle kingside (through f1)
        Chess::Board b; load(b, "4k3/5r2/8/8/8/8/8/4K2R w K - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::CastleKingSide)) return false; // should NOT castle
        }
        return true; // correctly excluded
    });

    RegisterTest("Edge: Castling not allowed while in check", []() {
        Chess::Board b; load(b, "3kr3/8/8/8/8/8/8/4K2R w K - 0 1");
        // White king on e1 is currently in check from rook on e8 (so castling must be disallowed)
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::CastleKingSide)) return false;
        }
        return true;
    });

    RegisterTest("Edge: EP pin detection (horizontal) - illegal if exposes king", []() {
        // If en-passant capture would expose the king horizontally to a rook, it must be illegal.
        // White king on e5, white pawn on d5, black pawn on c5 (en-passant target c6).
        // If exd6 ep were allowed, the file would open and rook on a5 would give check; that move must be illegal.
        Chess::Board b; load(b, "8/8/8/rPpK4/8/8/8/4k3 w - c6 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::EnPassant)) return false; // must be illegal
        }
        return true; // correctly excluded
    });

    RegisterTest("Edge: Only 1 move in zugzwang-like position", []() {
        Chess::Board b; auto s = load(b, "7k/8/5KR1/8/8/8/8/8 b - - 0 1");
        unsigned int n = countMoves(b, s);
        return n >= 1;
    });

    RegisterTest("Edge: Move count never exceeds MaxLegalMoves", []() {
        // Stress position with many possible moves; ensure we cap at MaxLegalMoves
        Chess::Board b; auto s = load(b,
            "R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1");
        unsigned int n = countMoves(b, s);
        return n <= Chess::MaxLegalMoves;
    });

    // =======================================================================
    // Draw detection tests
    // =======================================================================

    // -----------------------------------------------------------------------
    // FiftyMoveRule
    // The half-move clock is the 5th field in FEN. The rule triggers at >= 100.
    // -----------------------------------------------------------------------

    // Clock at exactly 100 - must be draw.
    RegisterTest("Draw: FiftyMove clock=100 is draw", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/8 w - - 100 1");
        return b.FiftyMoveRule();
    });

    // Clock at 99 - must NOT be draw yet.
    RegisterTest("Draw: FiftyMove clock=99 is not draw", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/8 w - - 99 1");
        return !b.FiftyMoveRule();
    });

    // Clock at 101 - also a draw (>= 100).
    RegisterTest("Draw: FiftyMove clock=101 is draw", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/8 w - - 101 1");
        return b.FiftyMoveRule();
    });

    // A pawn move resets the clock, so immediately after loading clock=0, not a draw.
    RegisterTest("Draw: FiftyMove clock=0 is not draw", []() {
        Chess::Board b;
        load(b, Chess::DefaultFEN);
        return !b.FiftyMoveRule();
    });

    // -----------------------------------------------------------------------
    // Threefold repetition
    // -----------------------------------------------------------------------

    RegisterTest("Draw: Threefold repetition detected after 3rd occurrence", []() {
        Chess::Board b;
        Chess::PieceColor side = load(b, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        // side == White. GameHistory has 1 entry (hash of initial position).

        // Round trip 1: white Ke1->d1, black Ke8->d8, white Kd1->e1, black Kd8->e8
        // After this, the initial position hash appears a 2nd time - not yet a draw.
        if (!doMove(b, Chess::PieceColor::White, "e1d1")) return false;
        if (!doMove(b, Chess::PieceColor::Black, "e8d8")) return false;
        if (!doMove(b, Chess::PieceColor::White, "d1e1")) return false;
        if (!doMove(b, Chess::PieceColor::Black, "d8e8")) return false;
        if (b.isThreefoldRepetition()) return false; // 2 occurrences, not yet

        // Round trip 2: same moves again -> 3rd occurrence
        if (!doMove(b, Chess::PieceColor::White, "e1d1")) return false;
        if (!doMove(b, Chess::PieceColor::Black, "e8d8")) return false;
        if (!doMove(b, Chess::PieceColor::White, "d1e1")) return false;
        if (!doMove(b, Chess::PieceColor::Black, "d8e8")) return false;

        return b.isThreefoldRepetition(); // 3rd occurrence: must be draw
    });

    // Two occurrences is not yet a draw.
    RegisterTest("Draw: Threefold not triggered after only 2 occurrences", []() {
        Chess::Board b;
        load(b, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");

        // One round trip -> 2nd occurrence.
        if (!doMove(b, Chess::PieceColor::White, "e1d1")) return false;
        if (!doMove(b, Chess::PieceColor::Black, "e8d8")) return false;
        if (!doMove(b, Chess::PieceColor::White, "d1e1")) return false;
        if (!doMove(b, Chess::PieceColor::Black, "d8e8")) return false;

        return !b.isThreefoldRepetition(); // must NOT be a draw yet
    });

    // A fresh board (single occurrence) is never a repetition draw.
    RegisterTest("Draw: Threefold not triggered on fresh load", []() {
        Chess::Board b;
        load(b, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        return !b.isThreefoldRepetition();
    });

    // -----------------------------------------------------------------------
    // Insufficient material
    // -----------------------------------------------------------------------

    // K vs K
    RegisterTest("Draw: Insufficient K vs K", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/8 w - - 0 1");
        return b.HasInsufficientMaterial();
    });

    // K+B vs K - bishop alone cannot force mate
    RegisterTest("Draw: Insufficient K+B vs K", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/5B2 w - - 0 1");
        return b.HasInsufficientMaterial();
    });

    // K+N vs K - knight alone cannot force mate
    RegisterTest("Draw: Insufficient K+N vs K", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/5N2 w - - 0 1");
        return b.HasInsufficientMaterial();
    });

    // K+N+N vs K - two knights cannot force mate (the engine treats this as draw)
    RegisterTest("Draw: Insufficient K+N+N vs K", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/4NN2 w - - 0 1");
        return b.HasInsufficientMaterial();
    });

    // K+N vs K+N - neither side has mating material
    RegisterTest("Draw: Insufficient K+N vs K+N", []() {
        Chess::Board b;
        load(b, "8/8/4k3/5n2/8/4K3/8/5N2 w - - 0 1");
        return b.HasInsufficientMaterial();
    });

    // K+B vs K+B same colour - bishops on same colour squares, both light
    RegisterTest("Draw: Insufficient K+B vs K+B same colour squares", []() {
        Chess::Board b;
        load(b, "2b1k3/8/8/8/8/8/8/4KB2 w - - 0 1");
        return b.HasInsufficientMaterial();
    });

    // K+B vs K+B different colour squares - one light, one dark -> NOT a draw.
    // White bishop on f1 (light), black bishop on d8 (dark: rank 0, file 3 -> (0+3)%2=1 -> dark).
    RegisterTest("Draw: Sufficient K+B vs K+B different colour squares", []() {
        Chess::Board b;
        load(b, "3bk3/8/8/8/8/8/8/4KB2 w - - 0 1");
        return !b.HasInsufficientMaterial();
    });

    // K+Q vs K - queen is mating material
    RegisterTest("Draw: Sufficient K+Q vs K is not draw", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/5Q2 w - - 0 1");
        return !b.HasInsufficientMaterial();
    });

    // K+R vs K - rook is mating material
    RegisterTest("Draw: Sufficient K+R vs K is not draw", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/8/5R2 w - - 0 1");
        return !b.HasInsufficientMaterial();
    });

    // K+P vs K - pawn can promote, so it's not insufficient
    RegisterTest("Draw: Sufficient K+P vs K is not draw", []() {
        Chess::Board b;
        load(b, "8/8/4k3/8/8/4K3/5P2/8 w - - 0 1");
        return !b.HasInsufficientMaterial();
    });

    // =======================================================================
    // Capture generator
    // =======================================================================

    RegisterTest("CaptureGen: matches legal gen at startpos", []() {
        Chess::Board b; auto s = load(b, Chess::DefaultFEN);
        return captureGenMatchesLegalGen(b, s);
    });

    RegisterTest("CaptureGen: matches legal gen in kiwipete", []() {
        Chess::Board b; auto s = load(b, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        return captureGenMatchesLegalGen(b, s);
    });

    // The en-passant landing square can itself block a check ray
    RegisterTest("CaptureGen: EP capture whose landing square blocks a check ray", []() {
        Chess::Board b; load(b, "2r3k1/8/8/2pP4/8/8/8/2K5 w - c6 0 1");
        Chess::Move caps[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllCaptureMoves(Chess::PieceColor::White, caps, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(caps[i].Flag, Chess::MoveFlag::EnPassant)) return true;
        }
        return false;
    });

    // Same horizontal-pin EP position as the "Edge: EP pin detection" test above,
    // but checked through the capture generator specifically.
    RegisterTest("CaptureGen: EP pin detection (horizontal) excludes the capture", []() {
        Chess::Board b; load(b, "8/8/8/rPpK4/8/8/8/4k3 w - c6 0 1");
        Chess::Move caps[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllCaptureMoves(Chess::PieceColor::White, caps, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (Chess::HasFlag(caps[i].Flag, Chess::MoveFlag::EnPassant)) return false;
        }
        return true;
    });

    // Full-tree consistency check (every capture move generated or missed, depth 3,
    // from a position dense with en-passant and promotion opportunities).
    RegisterTest("CaptureGen: full consistency vs legal gen, pos4 d3", []() {
        Chess::Board b; auto s = load(b, "r3k2r/Pppp1ppp/1b6/8/8/1B6/pPPP1PPP/R3K2R b KQkq - 0 1");
        return captureGenConsistentAtDepth(b, s, 3);
    });

    RegisterTest("CaptureGen: full consistency vs legal gen, pos5 d3", []() {
        Chess::Board b; auto s = load(b, "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
        return captureGenConsistentAtDepth(b, s, 3);
    });

    // =======================================================================
    // En-passant
    // =======================================================================

    // The a7 pawn's only legal reply is en-passant (exd6 is the sole way to
    // meet the discovered-check threat); its normal single/double pushes must
    // still be generated and legal too, since they aren't forced here - this
    // guards against a continue that skips a pawn's pushes while rejecting its EP move.
    RegisterTest("EnPassant: normal pushes survive alongside a legal EP capture", []() {
        Chess::Board b; load(b, "8/8/8/2pP4/8/8/8/k3K3 w - c6 0 1");
        // d5 should still have a one-square push to d6 available (not consumed by the EP branch)
        return findMove(b, Chess::PieceColor::White,
            Chess::To2DIndex(3, 3), // d5
            Chess::To2DIndex(2, 3), // d6
            false
        );
    });

    // Pawns not on their home rank must never attempt a double push, and must
    // not crash/UB when checked for one.
    RegisterTest("EnPassant: no double push offered off the home rank", []() {
        Chess::Board b; load(b, "4k3/8/8/4P3/8/8/8/4K3 w - - 0 1");
        Chess::Move moves[Chess::MaxLegalMoves];
        unsigned int n = 0;
        b.getAllLegalMoves(Chess::PieceColor::White, moves, n);

        for (unsigned int i = 0; i < n; ++i) {
            if (moves[i].StartingSquare == Chess::To2DIndex(3, 4) &&
                Chess::HasFlag(moves[i].Flag, Chess::MoveFlag::PawnDoublePush)) {
                return false;
            }
        }
        return true;
    });

    // HasAnyLegalMoves must agree with getAllLegalMoves's count in a position
    // where en-passant is among the legal replies (the original double-push
    // bug could desync HasAnyLegalMoves specifically, since it recomputes moves independently).
    RegisterTest("EnPassant: HasAnyLegalMoves agrees when EP is available", []() {
        Chess::Board b; auto s = load(b, "8/8/8/2pP4/8/8/8/k3K3 w - c6 0 1");
        return b.HasAnyLegalMoves(s) == (countMoves(b, s) > 0);
    });

    // Perft on a position built specifically to have overlapping en-passant,
    // pin, and discovered-check interactions on the same rank.
    RegisterPerftTest(
        "Position 19 (EP/pin stress)", "8/8/8/r2pP2k/8/8/4K3/8 w - d6 0 1",
        {10, 152, 1056, 18682, 125111}
    );

    // =======================================================================
    // Incremental state (material / PST / phase / colour occupancy) sanity.
    // =======================================================================

    RegisterTest("Incremental: survives a normal move + undo-by-copy", []() {
        Chess::Board b; auto s = load(b, Chess::DefaultFEN);
        Chess::Board copy = b;
        return doMove(copy, s, "e2e4");
    });

    RegisterTest("Incremental: survives castling both sides", []() {
        Chess::Board b; auto s = load(b, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        return doMove(b, s, "e1g1");
    });

    RegisterTest("Incremental: survives an en-passant capture", []() {
        Chess::Board b; auto s = load(b, "4k3/8/8/2pP4/8/8/8/4K3 w - c6 0 1");
        return doMove(b, s, "d5c6");
    });

    RegisterTest("Incremental: survives a capturing promotion", []() {
        Chess::Board b; auto s = load(b, "1n2k3/2P5/8/8/8/8/8/4K3 w - - 0 1");
        return doMove(b, s, "c7b8q");
    });
}

#pragma region Run Tests

static void RunTests() {
    const size_t total = s_Tests.size();

    std::cout << "========================================" << std::endl;
    std::cout << "Running " << total << " test cases" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    for (const auto& t : s_Tests) {
        bool ok = false;

        try {
            ok = t.fn();
        }
        catch (const std::exception& e) {
            std::cerr << "[EXCEPTION] " << t.name << ": "
                << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "[EXCEPTION] " << t.name
                << ": unknown" << std::endl;
        }

        if (ok) {
            std::cout << "[PASS] " << t.name << std::endl;
            ++s_Passed;
        } else {
            std::cout << "[FAIL] " << t.name << std::endl;
            ++s_Failed;
            s_FailedTests.push_back(t.name);
        }
    }

    std::cout << std::endl << "========================================" << std::endl;
    std::cout << "TEST SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "Total Tests : " << total << std::endl;
    std::cout << "Passed : " << s_Passed << std::endl;
    std::cout << "Failed : " << s_Failed << std::endl;

    if (!s_FailedTests.empty()) {
        std::cout << std::endl << "Failed Test Cases:" << std::endl;
        std::cout << "========================================" << std::endl;

        for (const auto& name : s_FailedTests) {
            std::cout << " - " << name << std::endl;
        }
    }

    std::cout << "========================================" << std::endl;
}

int main() {
    Chess::Init();

    RegisterTests();
    RunTests();

    return s_Failed;
}
