#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

namespace Chess {
    constexpr inline const int Ranks = 8;
    constexpr inline const int Files = 8;

    constexpr inline const int NullPos = Ranks * Files;

    // the maximum number of legal moves possible from the starting position is 218
    // however some buffer is kept in case playing a variant or something
    constexpr inline const int MaxLegalMoves = 256;

    constexpr inline const char* DefaultFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    enum class MoveFlag : uint16_t {
        None = 0,

        EnPassant = 1 << 0,
        PawnDoublePush = 1 << 1,
        Capture = 1 << 2,
        Check = 1 << 3,

        CastleKingSide = 1 << 4,
        CastleQueenSide = 1 << 5,

        PromoteToKnight = 1 << 6,
        PromoteToBishop = 1 << 7,
        PromoteToRook = 1 << 8,
        PromoteToQueen = 1 << 9,
    };

    enum class PieceType : uint8_t {
        Pawn, Bishop, Knight,
        Rook, Queen, King,
        None
    };

    enum class PieceColor : uint8_t {
        White, Black
    };

    namespace CastlingRights {
        constexpr inline const uint8_t WhiteKingSideBit = 0b0001;
        constexpr inline const uint8_t WhiteQueenSideBit = 0b0010;
        constexpr inline const uint8_t BlackKingSideBit = 0b0100;
        constexpr inline const uint8_t BlackQueenSideBit = 0b1000;
    }

    constexpr inline const MoveFlag PromotionFlags[] = {
        MoveFlag::PromoteToQueen, MoveFlag::PromoteToKnight,
        MoveFlag::PromoteToRook, MoveFlag::PromoteToBishop
    };

    constexpr inline const PieceType PromotionTypes[] = {
        PieceType::Queen, PieceType::Knight,
        PieceType::Rook, PieceType::Bishop
    };

    constexpr inline const uint16_t PieceValues[] = {
        100, 275, 300, 350, 620, 0
    };

    constexpr inline const uint16_t PieceEgValues[] = {
        100, 400, 300, 600, 1000, 0
    };

    struct Piece {
        Piece() = default;
        Piece(PieceType type, PieceColor color) : Type(type), Color(color) {}

        PieceType Type;
        PieceColor Color;
    };

    struct Move {
        Move() = default;
        Move(uint8_t start, uint8_t target) : StartingSquare(start), TargetSquare(target) {}
        Move(uint8_t start, uint8_t target, MoveFlag flag)
            : StartingSquare(start), TargetSquare(target), Flag(flag) {}

        uint8_t StartingSquare{NullPos};
        uint8_t TargetSquare{NullPos};

        MoveFlag Flag{MoveFlag::None};

        [[nodiscard]]
        inline bool operator==(Move other) const noexcept {
            return StartingSquare == other.StartingSquare && TargetSquare == other.TargetSquare;
        }

        [[nodiscard]]
        inline operator bool() const noexcept {
            return StartingSquare != TargetSquare;
        }
    };

    [[nodiscard]]
    constexpr inline int To2DIndex(int rank, int file) noexcept {
        return rank * Files + file;
    }

    [[nodiscard]]
    constexpr inline int StringNotationToIndex(char fileChar, char rankChar) noexcept {
        return To2DIndex(Ranks - 1 - rankChar + '1', fileChar - 'a');
    }

    [[nodiscard]]
    constexpr inline uint64_t IndexToMask(int idx) noexcept {
        return 1ull << idx;
    }

    [[nodiscard]]
    constexpr inline int MaskToIndex(uint64_t mask) noexcept {
        return __builtin_ctzll(mask);
    }

    [[nodiscard]]
    constexpr inline int NumBitsOn(uint64_t mask) noexcept {
        return __builtin_popcountll(mask);
    }

    [[nodiscard]]
    constexpr inline int ToRank(int idx) noexcept {
        return idx / Files;
    }

    [[nodiscard]]
    constexpr inline int ToFile(int idx) noexcept {
        return idx % Files;
    }

    [[nodiscard]]
    constexpr inline PieceColor InvertColor(PieceColor color) noexcept {
        return color == PieceColor::White ? PieceColor::Black : PieceColor::White;
    }

    [[nodiscard]]
    constexpr inline float CentripawnToUniform(int value) noexcept {
        constexpr float Scale = 1.f / 300.f;
        return std::clamp(1.f / (1.f + std::exp(-value * Scale)), 0.f, 1.f); // sigmoid
    }

    [[nodiscard]]
    constexpr inline bool HasFlag(MoveFlag value, MoveFlag flag) noexcept {
        return static_cast<uint16_t>(value) & static_cast<uint16_t>(flag);
    }

    [[nodiscard]]
    constexpr inline MoveFlag AddFlag(MoveFlag base, MoveFlag flag) noexcept {
        return static_cast<MoveFlag>(static_cast<uint16_t>(base) | static_cast<uint16_t>(flag));
    }

    [[nodiscard]]
    constexpr inline bool isPromotion(MoveFlag flag) noexcept {
        constexpr uint16_t Mask = (
            static_cast<uint16_t>(MoveFlag::PromoteToKnight) |
            static_cast<uint16_t>(MoveFlag::PromoteToBishop) |
            static_cast<uint16_t>(MoveFlag::PromoteToRook) |
            static_cast<uint16_t>(MoveFlag::PromoteToQueen)
        );

        return static_cast<uint16_t>(flag) & Mask;
    }

    [[nodiscard]]
    constexpr inline bool isCastle(MoveFlag flag) noexcept {
        constexpr uint16_t Mask = (
            static_cast<uint16_t>(MoveFlag::CastleKingSide) |
            static_cast<uint16_t>(MoveFlag::CastleQueenSide)
        );

        return static_cast<uint16_t>(flag) & Mask;
    }

    [[nodiscard]]
    inline std::string MoveToUCI(const Move& move) {
        if (!move) return "0000";

        const auto ToFileChar = [](int f) -> char { return 'a' + f; };
        const auto ToRankChar = [](int r) -> char { return '1' + (7 - r); };

        const int fromRank = ToRank(move.StartingSquare);
        const int fromFile = ToFile(move.StartingSquare);
        const int toRank = ToRank(move.TargetSquare);
        const int toFile = ToFile(move.TargetSquare);

        std::string uci;
        uci += ToFileChar(fromFile);
        uci += ToRankChar(fromRank);
        uci += ToFileChar(toFile);
        uci += ToRankChar(toRank);

        if (isPromotion(move.Flag)) {
            if (HasFlag(move.Flag, MoveFlag::PromoteToQueen)) uci += 'q';
            else if (HasFlag(move.Flag, MoveFlag::PromoteToRook)) uci += 'r';
            else if (HasFlag(move.Flag, MoveFlag::PromoteToKnight)) uci += 'n';
            else uci += 'b';
        }

        return uci;
    }

    [[nodiscard]]
    constexpr inline char PieceToChar(Piece p) noexcept {
        char c;

        if (p.Type == PieceType::Pawn) c = 'p';
        else if (p.Type == PieceType::Bishop) c = 'b';
        else if (p.Type == PieceType::Knight) c = 'n';
        else if (p.Type == PieceType::Rook) c = 'r';
        else if (p.Type == PieceType::Queen) c = 'q';
        else c = 'k';

        return p.Color == PieceColor::White ? std::toupper(c) : c;
    }

    class Board {
    private:
        // required to run tests
    public:
        struct PinMasks {
            uint64_t Pinned{0};
            uint64_t Rays[64]{0};
        };

        void scanPins(int kingPos, uint64_t pinners, uint64_t friendlyOcc, PinMasks& result) const;

        [[nodiscard]]
        PinMasks getPinMasks(PieceColor friendlyColor, uint64_t friendlyOcc, uint64_t totalOcc) const;

        [[nodiscard]]
        PinMasks getPinMasks(PieceColor friendlyColor) const;

        [[nodiscard]]
        uint64_t getCheckMask(PieceColor friendlyColor, uint64_t occ) const;

        [[nodiscard]]
        uint64_t getCheckMask(PieceColor friendlyColor) const;

        [[nodiscard]]
        uint64_t getCheckers(PieceColor friendlyColor) const;

        [[nodiscard]]
        uint64_t getAttackedSquares(PieceColor attacker, uint64_t occ) const;

        [[nodiscard]]
        uint64_t getAttackedSquares(PieceColor attacker) const;

        [[nodiscard]]
        bool isUnderAttack(int position, PieceColor attacker) const;

        struct CheckDetector {
            int KingSq;
            uint64_t KingMask;
            // squares where our pawn gives direct check
            uint64_t PawnChecks;
            // squares where our knight gives direct check
            uint64_t KnightChecks;
            // our pieces that may reveal a discovered check if moved
            uint64_t DcCandidates;
        };

        [[nodiscard]]
        CheckDetector buildCheckDetector(bool attackerIsBlack, int enemyKingSq, uint64_t occ, uint64_t friendlyOcc) const;

        [[nodiscard]]
        bool moveGivesCheck(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd) const;

        void pushMove(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd, Move* out, unsigned int& moveCount) const;
        void pushPromotions(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd, Move* out, unsigned int& moveCount) const;

        void generateMoves(
            PieceColor sideToMove, const PinMasks& pins,
            uint64_t checkMask, uint64_t attacked,
            uint64_t friendlyOcc, uint64_t enemyOcc, uint64_t occ,
            int filterIdx, // -1 = all pieces, >= 0 = only this square
            Move* out, unsigned int& moveCount
        ) const;

        void getAllLegalMoves(PieceColor sideToMove, Move* out, unsigned int& moveCount) const;
        void getAllCaptureMoves(PieceColor sideToMove, Move* out, unsigned int& moveCount) const;

        [[nodiscard]]
        unsigned int numPositions(int depth, PieceColor sideToMove);

        void doMoveInternal(Move move);

        void searchHelper(PieceColor sideToMove, int helperId) const;

        [[nodiscard]]
        float getEndgameWeight() const;

        [[nodiscard]]
        int evaluate(PieceColor sideToMove, float endgameWeight) const noexcept;

        [[nodiscard]]
        int evaluate(PieceColor sideToMove) const noexcept;

        [[nodiscard]]
        uint64_t seeGetAllAttackers(
            int pos, uint64_t knights, uint64_t diagSliders,
            uint64_t orthSliders, uint64_t kings, uint64_t occupancy
        ) const;

        [[nodiscard]]
        int see(Move move) const;

        [[nodiscard]]
        int scoreMove(Move move, PieceColor sideToMove) const noexcept;

        [[nodiscard]]
        int quiescence(int alpha, int beta, PieceColor sideToMove, int ply) const;

        [[nodiscard]]
        int negamax(int depth, int alpha, int beta, PieceColor sideToMove, int ply = 1, int ext = 0) const;

        [[nodiscard]]
        int evaluateMove(Move move, PieceColor sideToMove, int depth, int alpha, int beta) const;

        [[nodiscard]]
        bool isThreefoldRepetition(int ply) const;

        // Masks for 6 piece types for 2 colors
        uint64_t m_PieceMask[6 * 2];
        Piece m_Mailbox[Ranks * Files];

        // Special Moves
        uint8_t m_CastlingRights;
        uint8_t m_EnPassantSquare;

        // Rules
        uint64_t m_Hash;
        uint8_t m_HalfMoveClock;

        // Efficiency
        uint64_t m_PawnAndKingHash;

    public:
        void NewGame();

        // Returns side to move
        [[nodiscard]]
        PieceColor LoadFromFen(const std::string& fen);

        [[nodiscard]]
        std::string GetFen(PieceColor sideToMove) const;

        void DoMove(Move move);

        void FlipSideToMove();

        void Perft(int depth, PieceColor sideToMove);

        void PrintBoard(PieceColor sideToMove);

        void CancelSearch();

        [[nodiscard]]
        Move FindBestMoveByTime(PieceColor sideToMove, int thinkTimeMS, bool useOpeningBook = true) const;

        [[nodiscard]]
        Move FindBestMoveByDepth(PieceColor sideToMove, int targetDepth, bool useOpeningBook = true) const;

        [[nodiscard]]
        bool isUnderCheck(PieceColor sideToMove) const;

        [[nodiscard]]
        bool FiftyMoveRule() const noexcept;

        [[nodiscard]]
        bool isThreefoldRepetition() const noexcept;

        [[nodiscard]]
        bool HasInsufficientMaterial() const noexcept;

        [[nodiscard]]
        bool HasAnyLegalMoves(PieceColor sideToMove) const noexcept;

        [[nodiscard]]
        float GetConfidence(PieceColor sideToMove, int thinkTimeMS) const;

        [[nodiscard]]
        std::vector<Move> GetLegalMoves(int idx) const;

        [[nodiscard]]
        uint16_t GetMoveCount() const noexcept;

        [[nodiscard]]
        uint64_t GetOccupancyMap(PieceColor color) const noexcept;

        [[nodiscard]]
        uint64_t GetOccupancyMap() const noexcept;

        void SetContempt(int contempt) const;

        void SetThreads(unsigned int threads) const;

        [[nodiscard]]
        unsigned int GetThreads() const noexcept;

        void SetHashSize(unsigned int size) const;

        void ClearHash() const;

        void SetCatchAll(bool value) const;

        void SetEngineColor(PieceColor color) const;

        [[nodiscard]]
        unsigned int GetHashSize() const noexcept;

        [[nodiscard]]
        bool GetCatchAll() const noexcept;

        [[nodiscard]]
        inline uint8_t GetCastlingRights() const noexcept {
            return m_CastlingRights;
        }

        [[nodiscard]]
        inline uint8_t GetEnPassantSquare() const noexcept {
            return m_EnPassantSquare;
        }

        [[nodiscard]]
        inline uint64_t GetPieceMask(int idx) const noexcept {
            return m_PieceMask[idx];
        }

        [[nodiscard]]
        inline Piece GetPieceAt(int position) const noexcept {
            return m_Mailbox[position];
        }
    };

    extern void Init();
}
