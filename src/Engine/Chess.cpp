#include <iostream>
#include <cstring>
#include <random>
#include <atomic>
#include <thread>
#include <chrono>
#include <unordered_set>
#include <array>
#include <memory>
#include <functional>
#include <cstddef>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "Engine/Chess.hpp"

using namespace Chess;

#pragma region Low Level Helpers

[[nodiscard]]
static inline std::size_t FastRange(uint64_t key, std::size_t n) noexcept {
#if defined(__SIZEOF_INT128__)
    return static_cast<std::size_t>((static_cast<unsigned __int128>(key) * n) >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
    return static_cast<std::size_t>(__umulh(key, static_cast<uint64_t>(n)));
#else
    return static_cast<std::size_t>(key % n);
#endif
}

static inline void PrefetchAddress(const void* p) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(p);
#elif defined(_MSC_VER)
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_T0);
#else
    (void)p;
#endif
}

template <typename T, std::size_t N>
class UninitArray {
public:
    UninitArray() noexcept {}

    [[nodiscard]] inline T* data() noexcept {
        return reinterpret_cast<T*>(m_Storage);
    }
    [[nodiscard]] inline T& operator[](std::size_t i) noexcept {
        return data()[i];
    }
    [[nodiscard]] inline operator T* () noexcept {
        return data();
    }

private:
    alignas(T) unsigned char m_Storage[sizeof(T) * N];
};

#pragma region Utils

namespace Utils {
    [[nodiscard]]
    static std::vector<std::string> Tokenize(const std::string& string, char sep = ' ') {
        std::vector<std::string> splitted;

        std::string buffer;
        buffer.reserve(string.size());

        for (const char c : string) {
            if (c == sep) {
                if (!buffer.empty()) {
                    splitted.push_back(buffer);
                    buffer.clear();
                }
            } else {
                buffer.push_back(c);
            }
        }

        if (!buffer.empty()) splitted.push_back(buffer);

        return splitted;
    }

    [[nodiscard]]
    static constexpr inline int PieceToBitIdx(PieceType type, PieceColor color) noexcept {
        const int pt = static_cast<int>(type);
        return color == PieceColor::Black ? (pt + 6) : pt;
    }

    [[nodiscard]]
    static constexpr inline PieceType PromotionPiece(MoveFlag flag) noexcept {
        if (HasFlag(flag, MoveFlag::PromoteToQueen)) return PieceType::Queen;
        if (HasFlag(flag, MoveFlag::PromoteToRook)) return PieceType::Rook;
        if (HasFlag(flag, MoveFlag::PromoteToKnight)) return PieceType::Knight;
        return PieceType::Bishop;
    }

    [[nodiscard]]
    static constexpr inline int Mirror(int idx) noexcept {
        return idx ^ 56;
    }

    [[nodiscard]]
    static constexpr inline uint64_t FileMask(int file) noexcept {
        return 0x0101010101010101ull << file;
    }

    [[nodiscard]]
    static constexpr inline int IntLerp(int start, int end, float t) noexcept {
        return start + (end - start) * t;
    }
}

#pragma region Options

namespace Options {
    static int Contempt{50};
    static int Threads{1};
};

#pragma region Hashing

class ZobristHasher {
private:
    static inline uint64_t PieceHash[12][Ranks * Files];
    static inline uint64_t SideHash;
    static inline uint64_t CastlingHash[16];
    static inline uint64_t EnPassantHash[Files];

public:
    static void Init() {
        std::unordered_set<uint64_t> usedHashes;
        usedHashes.reserve(12 * Ranks * Files + 1 + 16 + Files);

        std::seed_seq seed{0x517CC1B727220A95ull, 0x3C6EF372FE94F82Aull};
        std::mt19937_64 gen(seed);

        std::random_device rd;

        const auto GetUniqueRandomHash = [&]() -> uint64_t {
            uint64_t hash;

            do {
                hash = gen();
            } while (usedHashes.contains(hash));

            usedHashes.insert(hash);

            return hash;
            };

        for (int pieceType = 0; pieceType < 12; ++pieceType) {
            for (int square = 0; square < Ranks * Files; ++square) {
                PieceHash[pieceType][square] = GetUniqueRandomHash();
            }
        }

        SideHash = GetUniqueRandomHash();

        for (auto& c : CastlingHash) {
            c = GetUniqueRandomHash();
        }

        for (auto& c : EnPassantHash) {
            c = GetUniqueRandomHash();
        }
    }

    static inline uint64_t HashSideToMove(uint64_t hash) noexcept {
        return hash ^ SideHash;
    }

    static inline uint64_t HashCastlingRights(uint64_t hash, uint8_t castlingRights) {
        return hash ^ CastlingHash[castlingRights];
    }

    static inline uint64_t HashEnPassantSquare(uint64_t hash, std::size_t enPassantSquare) noexcept {
        const int enPassantFile = enPassantSquare % Files;
        return hash ^ EnPassantHash[enPassantFile];
    }

    static inline uint64_t HashPiece(uint64_t hash, Piece piece, int sq) {
        return hash ^ PieceHash[Utils::PieceToBitIdx(piece.Type, piece.Color)][sq];
    }

    static uint64_t GetHash(const Board& board, PieceColor sideToMove) {
        uint64_t hash = 0ull;

        // hash for all piece types
        for (int pieceIdx = 0; pieceIdx < 12; ++pieceIdx) {
            for (uint64_t mask = board.GetPieceMask(pieceIdx); mask; mask &= mask - 1) {
                hash ^= PieceHash[pieceIdx][MaskToIndex(mask)];
            }
        }

        // side to move hash
        if (sideToMove == PieceColor::Black) hash ^= SideHash;

        // castling hash
        hash ^= CastlingHash[board.GetCastlingRights()];

        // en-passant hash
        if (board.GetEnPassantSquare() != NullPos) {
            hash = HashEnPassantSquare(hash, board.GetEnPassantSquare());
        }

        return hash;
    }

    static uint64_t GetPawnAndKingHash(const Board& board) {
        uint64_t hash = 0ull;

        // white pawns
        for (uint64_t mask = board.GetPieceMask(0); mask; mask &= mask - 1) {
            hash ^= PieceHash[0][MaskToIndex(mask)];
        }

        // white king
        hash ^= PieceHash[5][MaskToIndex(board.GetPieceMask(5))];

        // black pawns
        for (uint64_t mask = board.GetPieceMask(6); mask; mask &= mask - 1) {
            hash ^= PieceHash[6][MaskToIndex(mask)];
        }

        // black king
        hash ^= PieceHash[11][MaskToIndex(board.GetPieceMask(11))];

        return hash;
    }
};

#pragma region Transposition Table

class TranspositionTable {
public:
    enum class TTFlag : uint8_t {
        Exact,
        LowerBound,
        UpperBound
    };

    struct TTMove {
        uint8_t StartingSquare{NullPos};
        uint8_t TargetSquare{NullPos};
    };

    struct TTEntry {
        int32_t Score{0};
        int8_t Depth{-1};
        TTFlag NodeFlag{TTFlag::Exact};
        uint8_t Age{0};
        TTMove BestMove;
    };

private:

    struct alignas(16) Slot {
        std::atomic<uint64_t> Check{0};
        std::atomic<uint64_t> Data{0};
    };

    std::unique_ptr<Slot[]> m_Slots;
    std::size_t m_Count{0};
    uint8_t m_Age{0};

    [[nodiscard]]
    static constexpr uint64_t Pack(const TTEntry& e) noexcept {
        return static_cast<uint64_t>(static_cast<uint32_t>(e.Score)) |
            (static_cast<uint64_t>(e.BestMove.StartingSquare) << 32) |
            (static_cast<uint64_t>(e.BestMove.TargetSquare) << 40) |
            (static_cast<uint64_t>(static_cast<uint8_t>(e.Depth)) << 48) |
            (static_cast<uint64_t>(static_cast<uint8_t>(e.NodeFlag) & 3u) << 56) |
            (static_cast<uint64_t>(e.Age & 63u) << 58);
    }

    [[nodiscard]]
    static constexpr TTEntry Unpack(uint64_t d) noexcept {
        TTEntry e;
        e.Score = static_cast<int32_t>(static_cast<uint32_t>(d & 0xFFFFFFFFull));
        e.BestMove.StartingSquare = static_cast<uint8_t>((d >> 32) & 0xFF);
        e.BestMove.TargetSquare = static_cast<uint8_t>((d >> 40) & 0xFF);
        e.Depth = static_cast<int8_t>((d >> 48) & 0xFF);
        e.NodeFlag = static_cast<TTFlag>((d >> 56) & 3u);
        e.Age = static_cast<uint8_t>((d >> 58) & 63u);
        return e;
    }

public:
    void Clear() {
        for (std::size_t i = 0; i < m_Count; ++i) {
            m_Slots[i].Check.store(0, std::memory_order_relaxed);
            m_Slots[i].Data.store(0, std::memory_order_relaxed);
        }
    }

    // called once per new search so that entries from earlier searches can be replaced
    void NewSearch() noexcept {
        m_Age = static_cast<uint8_t>((m_Age + 1) & 63u);
    }


    inline void Prefetch(uint64_t key) const noexcept {
        if (!m_Count) return;
        PrefetchAddress(&m_Slots[FastRange(key, m_Count)]);
    }

    bool Peek(uint64_t key, TTEntry& out) const noexcept {
        if (!m_Count) return false;

        const Slot& slot = m_Slots[FastRange(key, m_Count)];
        const uint64_t data = slot.Data.load(std::memory_order_relaxed);
        const uint64_t check = slot.Check.load(std::memory_order_relaxed);

        if ((check ^ data) != key) return false;

        out = Unpack(data);
        return true;
    }

    bool Probe(uint64_t key, int depth, int alpha, int beta, int& score, Move& bestMove) const {
        TTEntry entry;
        if (!Peek(key, entry)) return false;

        // move isn't strict to depth since it's used for move ordering
        // even if it's not good it has a higher chance of being better than the other moves
        bestMove.StartingSquare = entry.BestMove.StartingSquare;
        bestMove.TargetSquare = entry.BestMove.TargetSquare;

        if (entry.Depth < depth) return false;

        if (entry.NodeFlag == TTFlag::Exact) {
            score = entry.Score;
            return true;
        } else if (entry.NodeFlag == TTFlag::LowerBound) {
            if (entry.Score >= beta) {
                score = entry.Score;
                return true;
            }
        } else if (entry.NodeFlag == TTFlag::UpperBound) {
            if (entry.Score <= alpha) {
                score = entry.Score;
                return true;
            }
        }

        return false;
    }

    void Store(uint64_t key, int depth, int score, TTFlag flag, Move bestMove) {
        if (!m_Count) return;

        Slot& slot = m_Slots[FastRange(key, m_Count)];
        const TTEntry existing = Unpack(slot.Data.load(std::memory_order_relaxed));

        // replace if the slot is from an older search, or if the new result is at least as deep
        if (existing.Age != m_Age || existing.Depth <= depth) {
            TTEntry e;
            e.Score = score;
            e.Depth = static_cast<int8_t>(depth);
            e.NodeFlag = flag;
            e.Age = m_Age;
            e.BestMove = TTMove(bestMove.StartingSquare, bestMove.TargetSquare);

            const uint64_t data = Pack(e);
            slot.Data.store(data, std::memory_order_relaxed);
            slot.Check.store(key ^ data, std::memory_order_relaxed);
        }
    }

    void SetSize(unsigned int sizeMb) {
        m_Count = std::max<std::size_t>(1, (static_cast<std::size_t>(sizeMb) * 1024 * 1024) / sizeof(Slot));
        m_Slots.reset(new Slot[m_Count]);
        Clear();
    }

    [[nodiscard]]
    inline unsigned int GetSize() const noexcept {
        return static_cast<unsigned int>((m_Count * sizeof(Slot)) / 1024 / 1024);
    }
};

class PawnTranspositionTable {
public:
    struct PawnTTEntry {
        uint64_t Key{0};
        uint64_t Passed[2]{0, 0};
        int16_t Mg{0};
        int16_t Eg{0};
    };

private:
    std::vector<PawnTTEntry> m_Entries;

public:
    void Clear() {
        std::memset(m_Entries.data(), 0, m_Entries.size() * sizeof(PawnTTEntry));
    }

    const PawnTTEntry* Probe(uint64_t key) const noexcept {
        if (m_Entries.empty()) return nullptr;
        const PawnTTEntry& e = m_Entries[FastRange(key, m_Entries.size())];
        return (e.Key == key) ? &e : nullptr;
    }

    void Store(uint64_t key, const PawnTTEntry& entry) noexcept {
        if (m_Entries.empty()) return;
        PawnTTEntry& slot = m_Entries[FastRange(key, m_Entries.size())];
        slot = entry;
        slot.Key = key;
    }

    void SetSize(unsigned int sizeMb) {
        m_Entries.resize((sizeMb * 1024 * 1024) / sizeof(PawnTTEntry));
        Clear();
    }

    [[nodiscard]]
    inline unsigned int GetSize() const noexcept {
        return (m_Entries.size() * sizeof(PawnTTEntry)) / 1024 / 1024;
    }
};

#pragma region Magic Bitboard

namespace MagicBitboard {
    struct Magic {
        uint64_t Mask;
        uint64_t Number;
        int Shift;
        uint64_t* Attacks;
    };

    static constexpr inline const uint64_t OrthogonalMagicNumbers[64] = {
        0x808010a180024002, 0x8040002000401005, 0x0080200028805000, 0x0680080080100004,
        0x4200040820100200, 0x4600020090080904, 0x0200080402004081, 0x0100010008204082,
        0x2000802040008002, 0x0802804000200280, 0x0001003020010048, 0x0005000900201000,
        0x0061000800850050, 0x4802000200083004, 0x485200410c282200, 0x0000800850800100,
        0x0200208005400183, 0x0040010020410a80, 0x0408120040802200, 0x8001030022100088,
        0x0028008008800c00, 0x0000808002002c00, 0x40800400100b0688, 0x8020220034014181,
        0x0500229080004001, 0x4000200080400082, 0x0003001100200040, 0x1050001080800800,
        0x0514008080040800, 0x8040040080020080, 0x18420002000803e4, 0x00004842000400a9,
        0x4810400064800090, 0x0000201000400044, 0x1282801001802007, 0x1000821000800800,
        0x0000980081800400, 0x0800800a00804400, 0x4400084264001110, 0x0acd04410a00048c,
        0x1088804001208008, 0x08b000c0a0104004, 0x8010012000808010, 0x420100241001000a,
        0x0804008028008016, 0x40820090940a0008, 0x0012021008040001, 0x0880244d038a0004,
        0x0440224100820200, 0x4c01082083400500, 0x0408408010220200, 0x8024900080080280,
        0x0820841008010100, 0x8051000804000700, 0x40441002080d0400, 0x1a40800100004080,
        0x8441008000419163, 0x0080c1108100a206, 0x0001a00842001082, 0x20e0500005000921,
        0x0001001008000245, 0x9105000400460801, 0x08060000a4080102, 0x0011802041041882
    };

    static constexpr inline const int OrthogonalShifts[64] = {
        52, 53, 53, 53, 53, 53, 53, 52,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        53, 54, 54, 54, 54, 54, 54, 53,
        52, 53, 53, 53, 53, 53, 53, 52
    };

    static constexpr inline const uint64_t DiagonalMagicNumbers[64] = {
        0x0090240108046100, 0x0010161800508102, 0x0249480501008004, 0x802220820000a000,
        0x241088200c000000, 0x410208064a00c002, 0x0204190808240001, 0x00a0440201822000,
        0x8c00050802080203, 0x0040204201820184, 0x0024040810891210, 0x2056840400800080,
        0x2100420210000200, 0x2000050908c00010, 0x8100040084900810, 0x00100484c1101001,
        0x1710904050820090, 0x02c8022001010200, 0x0810040800202020, 0x0408400c04020888,
        0xa840800400a00000, 0x004280811000a000, 0x00009082c8080800, 0x0001000201820122,
        0x0020060150900208, 0x00501c1052240400, 0x0043100021004200, 0x304200600a008200,
        0x0209001005024000, 0x8041410226100201, 0x030c044201084629, 0x00040840888a0090,
        0x2044024104200404, 0x0088089844240100, 0x0001080210050400, 0x8480401808008200,
        0x2141010400020120, 0x0804010201a84804, 0x2004408200040100, 0x0090810040210400,
        0x0000882010004a02, 0x0030c8421008082c, 0x80013a4050000800, 0x0104482011001800,
        0x0080081020918400, 0x004010a480808500, 0x4128020c10480408, 0x0005560082020100,
        0x0004040402089001, 0x0002004404042000, 0x00611300a0902008, 0x0081801084110101,
        0x8181882020450004, 0x0000400408108081, 0x00100410a2820040, 0x2004034802108000,
        0x0809440084104200, 0x01002200540a0800, 0x803004808400c800, 0x0080000010228800,
        0x8100400018302400, 0x0001000408102110, 0x000020080e10c400, 0x440808010803210a
    };

    static constexpr inline const int DiagonalShifts[64] = {
        58, 59, 59, 59, 59, 59, 59, 58,
        59, 59, 59, 59, 59, 59, 59, 59,
        59, 59, 57, 57, 57, 57, 59, 59,
        59, 59, 57, 55, 55, 57, 59, 59,
        59, 59, 57, 55, 55, 57, 59, 59,
        59, 59, 57, 57, 57, 57, 59, 59,
        59, 59, 59, 59, 59, 59, 59, 59,
        58, 59, 59, 59, 59, 59, 59, 58
    };

    static uint64_t OrthogonalTable[64][4096];
    static uint64_t DiagonalTable[64][512];

    static Magic OrthogonalMagics[64];
    static Magic DiagonalMagics[64];

    static uint64_t OrthogonalMask(int sq) {
        uint64_t m = 0ull;

        const int r = ToRank(sq);
        const int f = ToFile(sq);

        for (int nr = r + 1; nr < Ranks - 1; ++nr) {
            m |= IndexToMask(To2DIndex(nr, f));
        }
        for (int nr = r - 1; nr > 0; --nr) {
            m |= IndexToMask(To2DIndex(nr, f));
        }
        for (int nf = f + 1; nf < Files - 1; ++nf) {
            m |= IndexToMask(To2DIndex(r, nf));
        }
        for (int nf = f - 1; nf > 0; --nf) {
            m |= IndexToMask(To2DIndex(r, nf));
        }

        return m;
    }

    static uint64_t DiagonalMask(int sq) {
        uint64_t m = 0ull;

        const int r = ToRank(sq);
        const int f = ToFile(sq);

        for (int nr = r + 1, nf = f + 1; nr < Ranks - 1 && nf < Files - 1; ++nr, ++nf) {
            m |= IndexToMask(To2DIndex(nr, nf));
        }
        for (int nr = r + 1, nf = f - 1; nr < Ranks - 1 && nf > 0; ++nr, --nf) {
            m |= IndexToMask(To2DIndex(nr, nf));
        }
        for (int nr = r - 1, nf = f + 1; nr > 0 && nf < Files - 1; --nr, ++nf) {
            m |= IndexToMask(To2DIndex(nr, nf));
        }
        for (int nr = r - 1, nf = f - 1; nr > 0 && nf > 0; --nr, --nf) {
            m |= IndexToMask(To2DIndex(nr, nf));
        }

        return m;
    }

    static uint64_t SlowOrthogonal(int sq, uint64_t occ) {
        uint64_t a = 0ull;

        const int r = ToRank(sq);
        const int f = ToFile(sq);

        for (int nr = r + 1; nr < Ranks; ++nr) {
            a |= IndexToMask(To2DIndex(nr, f)); if (occ & IndexToMask(To2DIndex(nr, f))) break;
        }
        for (int nr = r - 1; nr >= 0; --nr) {
            a |= IndexToMask(To2DIndex(nr, f)); if (occ & IndexToMask(To2DIndex(nr, f))) break;
        }
        for (int nf = f + 1; nf < Files; ++nf) {
            a |= IndexToMask(To2DIndex(r, nf)); if (occ & IndexToMask(To2DIndex(r, nf))) break;
        }
        for (int nf = f - 1; nf >= 0; --nf) {
            a |= IndexToMask(To2DIndex(r, nf)); if (occ & IndexToMask(To2DIndex(r, nf))) break;
        }

        return a;
    }

    static uint64_t SlowDiagonal(int sq, uint64_t occ) {
        uint64_t a = 0ull;

        const int r = ToRank(sq);
        const int f = ToFile(sq);

        for (int nr = r + 1, nf = f + 1; nr < Ranks && nf < Files; ++nr, ++nf) {
            a |= IndexToMask(To2DIndex(nr, nf)); if (occ & IndexToMask(To2DIndex(nr, nf))) break;
        }
        for (int nr = r + 1, nf = f - 1; nr < Ranks && nf >= 0; ++nr, --nf) {
            a |= IndexToMask(To2DIndex(nr, nf)); if (occ & IndexToMask(To2DIndex(nr, nf))) break;
        }
        for (int nr = r - 1, nf = f + 1; nr >= 0 && nf < Files; --nr, ++nf) {
            a |= IndexToMask(To2DIndex(nr, nf)); if (occ & IndexToMask(To2DIndex(nr, nf))) break;
        }
        for (int nr = r - 1, nf = f - 1; nr >= 0 && nf >= 0; --nr, --nf) {
            a |= IndexToMask(To2DIndex(nr, nf)); if (occ & IndexToMask(To2DIndex(nr, nf))) break;
        }

        return a;
    }

    static void Init() {
        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const auto Populate = [sq](
                Magic& m, uint64_t* table, uint64_t magic, int shift, uint64_t mask, bool isRook
            ) -> void {
                m.Mask = mask;
                m.Number = magic;
                m.Shift = shift;
                m.Attacks = table;

                uint64_t occ = 0ull;

                do {
                    table[(occ * magic) >> shift] = isRook ? SlowOrthogonal(sq, occ) : SlowDiagonal(sq, occ);
                    occ = (occ - mask) & mask;
                } while (occ);
                };

            Populate(OrthogonalMagics[sq], OrthogonalTable[sq], OrthogonalMagicNumbers[sq], OrthogonalShifts[sq], OrthogonalMask(sq), true);
            Populate(DiagonalMagics[sq], DiagonalTable[sq], DiagonalMagicNumbers[sq], DiagonalShifts[sq], DiagonalMask(sq), false);
        }
    }

    [[nodiscard]]
    static inline uint64_t OrthogonalAttacks(int sq, uint64_t occ) noexcept {
        const Magic& m = OrthogonalMagics[sq];
        return m.Attacks[((occ & m.Mask) * m.Number) >> m.Shift];
    }

    [[nodiscard]]
    static inline uint64_t DiagonalAttacks(int sq, uint64_t occ) noexcept {
        const Magic& m = DiagonalMagics[sq];
        return m.Attacks[((occ & m.Mask) * m.Number) >> m.Shift];
    }

    [[nodiscard]]
    static inline uint64_t CombinedAttacks(int sq, uint64_t occ) noexcept {
        return OrthogonalAttacks(sq, occ) | DiagonalAttacks(sq, occ);
    }
}

#pragma region Opening Book

class OpeningBook {
private:
    static constexpr inline const struct {
        uint64_t Hash;
        uint8_t StartSquare;
        uint8_t TargetSquare;
        uint16_t Flag;
    } OPENING_BOOK[] = {
        {0xa75f52d1e18b27, 6, 21, 0}, {0xa75f52d1e18b27, 10, 26, 2}, {0xa75f52d1e18b27, 12, 28, 2}, {0x2bee5d7b4561cc2, 28, 35, 4}, {0x39016ffa3210662, 35, 27, 0}, {0x412ce6b2789ad0c, 61, 43, 0}, {0x45c44f1d0ad2086, 57, 42, 0}, {0x615104003e70b1c, 5, 33, 0}, {0x615104003e70b1c, 10, 26, 2}, {0x7b133b9dcb5c23a, 30, 21, 4}, {0x86d1d953cab90c5, 5, 14, 0}, {0x8997ba98383ce0f, 5, 14, 0}, {0xb5de51adc22391f, 44, 35, 4}, {0xb732b11a9516884, 51, 43, 0}, {0xc67d52a4b0ef6ae, 58, 30, 0}, {0xd4392956fbd6c42, 4, 6, 80}, {0xdb4814479e7f5cb, 5, 33, 0}, {0xdb78ab6ccdbe91c, 49, 42, 4}, {0xf1d86670d982a1c, 50, 34, 2}, {0xf1d86670d982a1c, 54, 46, 0}, {0xf933cfe6dd95d99, 3, 24, 4}, {0x10c9c99e9093560c, 27, 34, 4},
        {0x10d22389c613c54a, 52, 36, 2}, {0x11485f4c22a42f1e, 6, 21, 0}, {0x132d7b00d4c5b8d9, 58, 30, 0}, {0x135188dfdcabe56b, 62, 45, 0}, {0x13cdd3ae501548b1, 30, 39, 0}, {0x14a2e5a092f0a16d, 61, 54, 0}, {0x14ed6afea82f7b4a, 61, 54, 0}, {0x1541837f3fb29829, 15, 23, 0}, {0x16abbe3e031d98a9, 6, 21, 0}, {0x17a1c96ac856e2e6, 48, 32, 2}, {0x183f91f954c78a70, 8, 16, 0}, {0x183f91f954c78a70, 27, 34, 4}, {0x18704c48ce62ed57, 4, 6, 80}, {0x18803601c2e139ad, 50, 34, 2}, {0x18e8a3baa768a648, 12, 28, 2}, {0x19c201fa3034c8f8, 34, 25, 4}, {0x19c201fa3034c8f8, 62, 45, 0}, {0x1a6a67a37cb1eabc, 6, 21, 0}, {0x1a6a67a37cb1eabc, 12, 20, 0}, {0x1a9c19031905d4fb, 57, 51, 0}, {0x1ad43b278a0c9f6c, 20, 27, 4}, {0x1b3ae88309834909, 4, 6, 80},
        {0x1b55bd02dd58a020, 6, 21, 0}, {0x1b58d91e16685942, 21, 27, 4}, {0x1c682bc75a6926c4, 6, 21, 0}, {0x1d47d3201931f131, 60, 62, 80}, {0x1d5755421be0adf6, 58, 49, 0}, {0x1da8f23f1ed7a204, 52, 36, 2}, {0x1ee942f46fd1de08, 10, 18, 0}, {0x2184ac937b6e7646, 5, 33, 4}, {0x21999d15274abf6f, 5, 14, 0}, {0x2224c1e2535e2ea9, 62, 45, 0}, {0x22a6f49209b46a7c, 11, 27, 2}, {0x22f1429d249fc564, 8, 24, 2}, {0x232fcdbaa0ae49a5, 12, 20, 0}, {0x23f6ef1079586faf, 14, 22, 0}, {0x2431b42ee96694c7, 4, 6, 80}, {0x25316ce06d28ce10, 57, 42, 0}, {0x255487098e50dfff, 48, 40, 0}, {0x255487098e50dfff, 52, 44, 0}, {0x255487098e50dfff, 54, 46, 0}, {0x255487098e50dfff, 57, 42, 0}, {0x25a5264df1f51e9f, 1, 18, 0}, {0x25afad7a8fdeedc3, 12, 28, 2},
        {0x26fc5d9e8a380744, 14, 22, 0}, {0x28cf3ccd4d62c08a, 33, 42, 12}, {0x2ae0abf4a90134d9, 5, 14, 0}, {0x2b77ce40bf5d0c6c, 21, 36, 0}, {0x2bd455b040dfb12e, 1, 18, 0}, {0x2c0f9f9340eb94e4, 1, 18, 0}, {0x2e1689f4d1f3c44e, 36, 42, 4}, {0x2e16d4aaf06237a9, 4, 6, 80}, {0x2ea9c6d3e55d4b59, 36, 27, 4}, {0x2ea9c6d3e55d4b59, 36, 28, 0}, {0x2ea9c6d3e55d4b59, 57, 42, 0}, {0x2ea9c6d3e55d4b59, 57, 51, 0}, {0x2f0ad829098b9e3f, 61, 52, 0}, {0x2fec01cb7122fc5c, 14, 22, 0}, {0x31a11292ab7b2346, 6, 21, 0}, {0x325f399644bb0c34, 60, 62, 80}, {0x326a302b6b823284, 12, 28, 2}, {0x32706d782acfa9ba, 58, 49, 0}, {0x3284673fbe0b2ea8, 61, 52, 0}, {0x33026d87f0aa9cda, 5, 19, 0}, {0x33026d87f0aa9cda, 11, 19, 0}, {0x331457a0b9fb0821, 6, 21, 0},
        {0x331457a0b9fb0821, 11, 27, 2}, {0x331457a0b9fb0821, 13, 29, 2}, {0x3427d7ac7ca30336, 53, 37, 2}, {0x3427d7ac7ca30336, 54, 46, 0}, {0x347cbd74bc6671b4, 52, 44, 0}, {0x3507cff80b24d0d8, 51, 35, 2}, {0x350e425408b2c454, 11, 19, 0}, {0x353b72157ea666c6, 25, 18, 4}, {0x353b72157ea666c6, 25, 32, 0}, {0x35e8db932dd2840d, 11, 27, 2}, {0x3607f894319ed08a, 45, 30, 0}, {0x367d727b84686b9a, 58, 49, 0}, {0x3690df3200a212cc, 6, 21, 0}, {0x3690df3200a212cc, 10, 26, 2}, {0x3690df3200a212cc, 11, 27, 2}, {0x377896dd4cd94e06, 5, 26, 0}, {0x37ed8bc2bc0adc17, 61, 54, 0}, {0x3978ada870f2c929, 48, 40, 0}, {0x3978ada870f2c929, 52, 44, 0}, {0x3978ada870f2c929, 54, 46, 0}, {0x3978ada870f2c929, 59, 50, 0}, {0x3978ada870f2c929, 62, 45, 0},
        {0x3a1c12e545631dd1, 6, 21, 0}, {0x3a1c12e545631dd1, 10, 18, 0}, {0x3a1c12e545631dd1, 12, 28, 2}, {0x3a8135d1673c5ead, 51, 43, 0}, {0x3bb93afb45242d46, 51, 35, 2}, {0x3bc6af36a91dbbf2, 53, 37, 2}, {0x3cc5c2cd55a90cec, 53, 37, 2}, {0x3cc5c2cd55a90cec, 62, 45, 0}, {0x3e183b20351ad08c, 58, 49, 0}, {0x3e32b0f308d33ad1, 51, 35, 2}, {0x3ea646133d18344c, 5, 14, 0}, {0x3f9849150edd33cc, 35, 27, 0}, {0x3fd3e849b6bb6d16, 61, 34, 4}, {0x3fe1d0b01f5e3188, 60, 62, 80}, {0x416d4ef3205420b1, 59, 52, 0}, {0x41fe319a2c79041a, 27, 34, 4}, {0x4206d024b37019e5, 57, 42, 0}, {0x42791db2d8ed4700, 34, 27, 4}, {0x42791db2d8ed4700, 58, 30, 0}, {0x42791db2d8ed4700, 58, 37, 0}, {0x42791db2d8ed4700, 62, 45, 0}, {0x42b0c5a8990c3782, 11, 27, 2},
        {0x43430c64165579b9, 1, 18, 0}, {0x43a1f2a04222088f, 49, 33, 2}, {0x43a1f2a04222088f, 49, 41, 0}, {0x43a1f2a04222088f, 50, 34, 2}, {0x43a1f2a04222088f, 51, 35, 2}, {0x43a1f2a04222088f, 52, 36, 2}, {0x43a1f2a04222088f, 53, 37, 2}, {0x43a1f2a04222088f, 54, 38, 2}, {0x43a1f2a04222088f, 54, 46, 0}, {0x43a1f2a04222088f, 62, 45, 0}, {0x43c104e73bf27e64, 6, 21, 0}, {0x45b26af8f0fdd076, 11, 27, 2}, {0x4849a2b341e99cda, 57, 42, 0}, {0x494bc19cee7685c1, 5, 14, 0}, {0x4971fec9e1d098fe, 52, 36, 2}, {0x49f4ef5f172916af, 5, 14, 0}, {0x4a5bc05295b25bab, 51, 43, 0}, {0x4b0c9ffbbefd2de8, 6, 21, 0}, {0x4c49803a3272d36a, 10, 26, 2}, {0x4d5fc34edc0f248f, 30, 37, 0}, {0x4e606909b0e86c8c, 41, 34, 4}, {0x4e8715f3f30a3a7f, 6, 21, 0},
        {0x4f08149cb602946c, 45, 35, 4}, {0x4f82d5f969f90bac, 27, 36, 4}, {0x5149feac050a9f76, 60, 62, 80}, {0x5188ffc2ea8a0427, 52, 36, 2}, {0x519acdfe1dc344d0, 4, 6, 80}, {0x51a2020996aacdc7, 60, 62, 80}, {0x51beea5f88a50275, 6, 21, 0}, {0x52b8650282d05d09, 62, 45, 0}, {0x52f7ea5cb80f872e, 52, 44, 0}, {0x54a8b08765caa52c, 62, 45, 0}, {0x5564b18db9e4a00b, 52, 36, 2}, {0x5564b18db9e4a00b, 54, 46, 0}, {0x5564b18db9e4a00b, 58, 30, 0}, {0x55ba36b7eae546b6, 5, 26, 0}, {0x55ba36b7eae546b6, 5, 33, 8}, {0x55ba36b7eae546b6, 6, 21, 0}, {0x55d9319feb452141, 1, 18, 0}, {0x561ee028e22e53e6, 61, 54, 0}, {0x561ee028e22e53e6, 62, 45, 0}, {0x57a9c5b9dcda9ffa, 30, 21, 4}, {0x582e2adbf74e2763, 2, 16, 4}, {0x5ac515c5cbeb90c4, 52, 36, 2},
        {0x5add2c5933a2e61f, 62, 45, 0}, {0x5b26b8414a317924, 5, 33, 8}, {0x5b26b8414a317924, 11, 27, 2}, {0x5b869bf35122ba05, 11, 19, 0}, {0x5cc93a1633f3c84a, 28, 19, 4}, {0x5db8ecd29ed05e34, 52, 44, 0}, {0x5e4283bbc99e2862, 6, 21, 0}, {0x5e4283bbc99e2862, 10, 26, 2}, {0x5e53861ad3a46b3c, 57, 42, 0}, {0x5e53861ad3a46b3c, 62, 45, 0}, {0x5eabd28f95825836, 62, 45, 0}, {0x6053aae7d6bfa843, 5, 12, 0}, {0x6053aae7d6bfa843, 21, 36, 4}, {0x610565c633594703, 12, 20, 0}, {0x610565c633594703, 14, 22, 0}, {0x63bfa6efe1d5e2fb, 1, 18, 0}, {0x6428113962abdf5f, 1, 11, 0}, {0x6428113962abdf5f, 2, 29, 0}, {0x6428113962abdf5f, 6, 21, 0}, {0x644b05a93924bb11, 58, 51, 0}, {0x6660a54d9e2bff28, 5, 14, 0}, {0x66b174cf68b9e762, 4, 6, 80},
        {0x670516d13758ceee, 61, 25, 0}, {0x671e560cd774023f, 54, 46, 0}, {0x67cfdaeec4a410b2, 61, 54, 0}, {0x68c4a7f90830a7a6, 10, 26, 2}, {0x6941a99958b85bca, 34, 27, 4}, {0x69f6816961020038, 11, 27, 2}, {0x6bed7c1eedb159ec, 51, 35, 2}, {0x6d4161da8ce2f714, 53, 37, 2}, {0x6d4161da8ce2f714, 53, 45, 0}, {0x6d4161da8ce2f714, 61, 52, 0}, {0x6d4161da8ce2f714, 62, 45, 0}, {0x6dd98e28e5e8bc0b, 58, 30, 0}, {0x6e6490909fd8ba63, 62, 45, 0}, {0x6e7891e43ea80e1f, 50, 42, 0}, {0x6e7891e43ea80e1f, 57, 42, 0}, {0x6e7891e43ea80e1f, 62, 45, 0}, {0x6e7bbaa8a02060cb, 61, 54, 0}, {0x6f5b4a313e31f0ff, 62, 45, 0}, {0x70bbd13b07fb80bd, 6, 21, 0}, {0x70bbd13b07fb80bd, 8, 16, 0}, {0x70bbd13b07fb80bd, 13, 29, 2}, {0x71bf717bf901fcd0, 12, 20, 0},
        {0x71f70b60ac2a2073, 37, 28, 4}, {0x720b40d74f8f1c6c, 51, 35, 2}, {0x720b40d74f8f1c6c, 61, 25, 0}, {0x720b40d74f8f1c6c, 61, 34, 0}, {0x73e104ceb5d75026, 2, 20, 0}, {0x7579a1234c2cad50, 57, 42, 0}, {0x75c4d96f3f48ebba, 52, 44, 0}, {0x75e5dea033476052, 12, 20, 0}, {0x75e5dea033476052, 14, 22, 0}, {0x75e5dea033476052, 21, 36, 0}, {0x75f9bc3f5a66ae33, 10, 19, 4}, {0x7747c7c87dcc1295, 11, 19, 0}, {0x787f0ed3f4925337, 52, 44, 0}, {0x787f0ed3f4925337, 57, 42, 0}, {0x793cf64b15c45a08, 9, 17, 0}, {0x7b46986a23c54a25, 62, 45, 0}, {0x7d85e6e01a64fb40, 49, 42, 4}, {0x7f06917ceff7acad, 54, 46, 0}, {0x814d3efeef8eeb93, 62, 45, 0}, {0x81ab48f60bdf8651, 53, 37, 2}, {0x81ab48f60bdf8651, 57, 42, 0}, {0x81ab48f60bdf8651, 62, 45, 0},
        {0x827236024832da5f, 6, 21, 0}, {0x82cdaca1416e9453, 49, 42, 4}, {0x850314773c8f466d, 10, 18, 0}, {0x850314773c8f466d, 12, 20, 0}, {0x850314773c8f466d, 27, 34, 4}, {0x850ba9ad535725c5, 6, 21, 0}, {0x86234d0b2f03f0c1, 54, 46, 0}, {0x86dcea762a34ff33, 58, 49, 0}, {0x87467e2c4835bbf9, 2, 9, 0}, {0x87467e2c4835bbf9, 2, 16, 0}, {0x87e008ff245d2659, 48, 32, 2}, {0x880f8d8926c67d86, 10, 26, 2}, {0x89740c75d13a54d5, 50, 42, 0}, {0x89a9730c00d6c856, 62, 45, 0}, {0x8a58f007c39fbb6e, 12, 20, 0}, {0x8a5fd2216e7460ab, 2, 38, 0}, {0x8a91383b86b84869, 26, 35, 4}, {0x8b475f1413604156, 50, 34, 2}, {0x8b475f1413604156, 52, 44, 0}, {0x8c056cf6ced40761, 5, 33, 8}, {0x8c056cf6ced40761, 9, 17, 0}, {0x8e3f8f3dacaa5367, 50, 34, 2},
        {0x8e66578fb8fa1a81, 5, 26, 0}, {0x8e66578fb8fa1a81, 11, 27, 2}, {0x8e66578fb8fa1a81, 28, 37, 4}, {0x8f72cf54ffa48ede, 36, 27, 4}, {0x8f72cf54ffa48ede, 36, 28, 0}, {0x8f72cf54ffa48ede, 53, 45, 0}, {0x8f72cf54ffa48ede, 57, 42, 0}, {0x935d4cff1750bc75, 62, 45, 0}, {0x93e3e579462044d9, 11, 19, 0}, {0x93e3e579462044d9, 12, 20, 0}, {0x93fe17ad0d32d00f, 1, 18, 0}, {0x942da12bbf2c4a75, 14, 22, 0}, {0x94dcc67f11dd155f, 51, 35, 2}, {0x953eb721b6426106, 5, 14, 0}, {0x9699db604a742f1e, 3, 27, 4}, {0x9699db604a742f1e, 6, 21, 0}, {0x9835b6189a01e71c, 4, 6, 80}, {0x985b00ae17177e26, 5, 14, 0}, {0x990dd5a0c7e143af, 14, 22, 0}, {0x991c960d35517be2, 52, 44, 0}, {0x9a71b1af909db81c, 6, 21, 0}, {0x9b10e5c2787ac5c2, 11, 18, 4},
        {0x9b6a548db697ad2f, 11, 27, 2}, {0x9bc036695f7744ce, 12, 21, 4}, {0x9c5047bc4e0f962e, 11, 27, 2}, {0x9c9171d0ae6a7a5a, 62, 45, 0}, {0x9cbbc288ce2e94d4, 28, 36, 0}, {0x9ce76f4c77b5cddc, 61, 54, 0}, {0x9d3be5cf6f751103, 5, 14, 0}, {0x9e24d272995da83d, 62, 45, 0}, {0x9e6941123f581ffa, 52, 36, 2}, {0x9eee34d2e658f418, 1, 18, 0}, {0x9f07a7d2e51d5ed7, 50, 42, 4}, {0xa0f7511706c2d0b6, 21, 27, 0}, {0xa22c6b0811166493, 50, 34, 2}, {0xa2b65b1e54e8b5da, 62, 45, 0}, {0xa2e426942525fb2b, 36, 27, 4}, {0xa3006e861122e99d, 5, 12, 0}, {0xa33624c56e5b5e90, 61, 54, 0}, {0xa5363ed50b8a2e30, 9, 17, 0}, {0xa6fd9803941f3369, 5, 12, 0}, {0xa6fd9803941f3369, 27, 34, 4}, {0xa8ce91c94d203044, 35, 18, 4}, {0xa93645062d2bfdbf, 8, 16, 0},
        {0xa978cc780e5be5ba, 11, 19, 0}, {0xaa816fcf9457403d, 36, 28, 0}, {0xaaf5ed62ad349af9, 29, 36, 4}, {0xab6a475645eb2514, 6, 21, 0}, {0xacca4d212f607487, 60, 62, 80}, {0xadf6cd4fb5bc395f, 58, 49, 0}, {0xae3756645c86b4c0, 61, 54, 0}, {0xaf23abb78bca81a1, 58, 30, 0}, {0xaf23abb78bca81a1, 59, 41, 0}, {0xaf408540c6606ac3, 61, 54, 0}, {0xafa67861bf8291cd, 52, 36, 2}, {0xafa67861bf8291cd, 54, 46, 0}, {0xafd7cd8b34e2df6b, 62, 45, 0}, {0xb0f95520dc006c05, 10, 26, 2}, {0xb1805f08e6d3e372, 57, 42, 0}, {0xb182d4dc2bfc4556, 53, 45, 0}, {0xb2822c3479d24d60, 62, 45, 0}, {0xb2967fb5db4edf40, 62, 45, 0}, {0xb2b716a594136e37, 10, 26, 2}, {0xb2b716a594136e37, 12, 20, 0}, {0xb2b716a594136e37, 14, 22, 0}, {0xb2bfab7ffbcb0d9f, 14, 22, 0},
        {0xb35f7098e8a91ea1, 11, 19, 0}, {0xb368d11ebdc1c554, 57, 51, 0}, {0xb368d11ebdc1c554, 58, 51, 0}, {0xb3b977a924298229, 55, 47, 0}, {0xb46f100aa00cee21, 61, 34, 0}, {0xb46f100aa00cee21, 62, 45, 0}, {0xb48176c35fbd7ba6, 4, 6, 80}, {0xb6ccd06e110843c2, 52, 36, 2}, {0xb72f672c32772d59, 6, 21, 0}, {0xb768f397ce5a8ae4, 5, 14, 0}, {0xb768f397ce5a8ae4, 11, 27, 2}, {0xb7a12b8d8fbbfa66, 62, 45, 0}, {0xba0f13a74aba2483, 1, 18, 0}, {0xba122221169eedaa, 5, 14, 0}, {0xbae668173b2b6d2c, 34, 27, 4}, {0xbb0f32a090f55aeb, 18, 27, 4}, {0xbb27e8151ff14104, 38, 30, 0}, {0xbc685241bf37f940, 27, 17, 0}, {0xbd2781003cc30fdb, 27, 24, 0}, {0xbd77e2aabbec5581, 14, 22, 0}, {0xbeb5a3999c370896, 61, 25, 0}, {0xbecf2ccf0345a3b8, 35, 18, 4},
        {0xbf56d76c9c38cfcc, 27, 35, 0}, {0xbf9a27a51d499ddd, 3, 12, 0}, {0xbf9a27a51d499ddd, 8, 24, 2}, {0xbf9a27a51d499ddd, 10, 26, 2}, {0xbf9a27a51d499ddd, 33, 51, 12}, {0xc08eef25e930b793, 11, 27, 2}, {0xc0eb46661439aff7, 52, 44, 0}, {0xc0f935b63b6549e9, 61, 43, 0}, {0xc1462e221adc1d5c, 30, 39, 0}, {0xc1da75539662b086, 60, 62, 80}, {0xc2423412da5a03e1, 27, 34, 4}, {0xc260abd15d00c022, 8, 16, 0}, {0xc2f7e9b5960a6a38, 45, 35, 4}, {0xc2f7e9b5960a6a38, 50, 42, 0}, {0xc2f7e9b5960a6a38, 61, 34, 0}, {0xc38131171615df28, 50, 34, 2}, {0xc38131171615df28, 54, 46, 0}, {0xc60b57235368a5c9, 1, 18, 0}, {0xc7754a3ff2e20316, 9, 25, 2}, {0xc7754a3ff2e20316, 12, 20, 0}, {0xc7a7522799cec503, 5, 19, 0}, {0xc7b301a63b525723, 4, 6, 80},
        {0xc7f0b0fda8e83ea0, 4, 6, 80}, {0xc873af0e555acecb, 61, 43, 0}, {0xc873af0e555acecb, 62, 52, 0}, {0xc95370d2db6675c4, 1, 18, 0}, {0xc95370d2db6675c4, 5, 26, 0}, {0xc95370d2db6675c4, 6, 21, 0}, {0xca4d7dffb21b651d, 33, 42, 12}, {0xcb09d50523ad1d68, 6, 21, 0}, {0xcb09d50523ad1d68, 11, 27, 2}, {0xcb09d50523ad1d68, 12, 28, 2}, {0xcbd8c0695f18190e, 3, 21, 4}, {0xcbe4784ca767643e, 54, 46, 0}, {0xccd5b5ec0cbe55b4, 34, 27, 4}, {0xccd5b5ec0cbe55b4, 62, 45, 0}, {0xcce1ed9c3152f250, 57, 42, 0}, {0xce8043b624c83578, 57, 42, 0}, {0xceb623fd9f2516d6, 35, 27, 0}, {0xceb623fd9f2516d6, 60, 62, 80}, {0xcf230fb3541ef7e9, 62, 45, 0}, {0xcf9f69cd58219495, 10, 26, 2}, {0xd1dd8c1e968fcfd0, 61, 54, 0}, {0xd391f3b52ff7fb06, 33, 24, 4},
        {0xd65e799a45611c91, 5, 19, 4}, {0xd67117cbf504f355, 10, 18, 0}, {0xd7352fc745e9e34d, 11, 27, 2}, {0xd787768c1668af99, 33, 42, 12}, {0xd845cb9ecaedb8e2, 6, 21, 0}, {0xd84bb9665dc5ed7f, 9, 17, 0}, {0xd87e060f2fdb54d2, 1, 18, 0}, {0xd9cd6978f582e514, 51, 35, 2}, {0xd9ea946c3748de8a, 4, 6, 80}, {0xda103dc4e29bd82e, 8, 16, 0}, {0xda103dc4e29bd82e, 14, 22, 0}, {0xda7e0feb1f688e37, 11, 19, 0}, {0xdae6e0197662c528, 4, 6, 80}, {0xdbc0a3f830e87f3d, 36, 21, 0}, {0xdbfaec9a8f8b6cbe, 48, 40, 0}, {0xdc0d8ca5a579ab08, 49, 33, 2}, {0xdc0d8ca5a579ab08, 50, 42, 0}, {0xdd1468592992c8ab, 14, 22, 0}, {0xdf3c773a8612bcf1, 49, 42, 4}, {0xdf74e7808ad73970, 20, 27, 4}, {0xe006c37482e16a35, 50, 34, 2}, {0xe02ca1b16c98d364, 6, 21, 0},
        {0xe02ca1b16c98d364, 10, 18, 0}, {0xe02ca1b16c98d364, 10, 26, 2}, {0xe02ca1b16c98d364, 11, 19, 0}, {0xe02ca1b16c98d364, 11, 27, 2}, {0xe02ca1b16c98d364, 12, 20, 0}, {0xe02ca1b16c98d364, 12, 28, 2}, {0xe11c8cb9fdac5036, 62, 45, 0}, {0xe12ac6fa82d5e73b, 11, 27, 2}, {0xe1b30b8584476c89, 11, 27, 2}, {0xe1b30b8584476c89, 12, 28, 2}, {0xe37abe16dfa03be0, 35, 27, 0}, {0xe469637fea0415f1, 11, 27, 2}, {0xe66c616d55d0a636, 10, 26, 2}, {0xe6794c3250dee81d, 60, 62, 80}, {0xe6c609f047f8db35, 11, 19, 0}, {0xe71ec0dddb749c4e, 34, 27, 4}, {0xe71ec0dddb749c4e, 52, 44, 0}, {0xe71ec0dddb749c4e, 57, 42, 0}, {0xe87a55e0c315e855, 2, 29, 0}, {0xe9364abc76013107, 60, 62, 80}, {0xeb5938b92813e1c7, 54, 46, 0}, {0xebdf1940a4d8ee5b, 62, 45, 0},
        {0xec3cf832856159ec, 62, 45, 0}, {0xecb6e62854761bc4, 25, 16, 4}, {0xed9c7478b18bb640, 61, 52, 0}, {0xedd2dcfaa6c2e0cd, 2, 9, 0}, {0xedfbfdb9ab0bbde9, 42, 36, 4}, {0xee069148f1354276, 37, 46, 0}, {0xee4e3f411f605901, 61, 52, 0}, {0xee59dc7e7300ce2b, 5, 33, 0}, {0xee59dc7e7300ce2b, 6, 21, 0}, {0xeed6d0b3766a121e, 60, 62, 80}, {0xeef11182e63fca6f, 61, 54, 0}, {0xf1158234f5767656, 26, 35, 4}, {0xf1b1bb5707d0ed9f, 50, 34, 2}, {0xf1b1bb5707d0ed9f, 58, 30, 0}, {0xf1b1bb5707d0ed9f, 58, 37, 0}, {0xf1fef65b5860f61b, 4, 6, 80}, {0xf1fef65b5860f61b, 10, 26, 2}, {0xf205acd3c430dfcc, 27, 42, 4}, {0xf3783776e681ed4c, 4, 6, 80}, {0xf44c53ab0a8225ba, 21, 27, 4}, {0xf47c136cad0ef1d0, 1, 18, 0}, {0xf555dcc114681dcf, 1, 18, 0},
        {0xf5761e9cd0d333a7, 49, 42, 4}, {0xf6078c6f0e941e76, 6, 21, 0}, {0xf6123098551bac45, 53, 45, 0}, {0xf7431b900ab2c01c, 62, 45, 0}, {0xf93441648c541d22, 54, 46, 0}, {0xf93441648c541d22, 57, 42, 0}, {0xf93441648c541d22, 62, 45, 0}, {0xf94846d4e5514b2f, 14, 22, 0}, {0xf9d4f20cb6a25689, 50, 34, 2}, {0xf9d4f20cb6a25689, 58, 37, 0}, {0xf9fc33ccf5945981, 21, 36, 0}, {0xfa249d3b7d6148c7, 51, 35, 2}, {0xfb7d15d55f1e36b8, 20, 27, 4}, {0xfbb584d9ae0d71f4, 6, 21, 0}, {0xfbb584d9ae0d71f4, 10, 26, 2}, {0xfbb584d9ae0d71f4, 11, 27, 2}, {0xfbb584d9ae0d71f4, 12, 28, 2}, {0xfc985e9e4256d215, 2, 29, 0}, {0xfc9d74789b3a91a3, 36, 42, 4}, {0xfcfae68794f44e86, 12, 20, 0}, {0xfd414d96d4d57369, 5, 12, 0}, {0xff6effb32cf47ae8, 28, 19, 4},
        {0xff6effb32cf47ae8, 50, 34, 2}, {0xff6effb32cf47ae8, 62, 45, 0}
    };

    static constexpr inline const size_t BOOK_SIZE = sizeof(OPENING_BOOK) / sizeof(OPENING_BOOK[0]);

public:
    [[nodiscard]]
    static bool Probe(uint64_t hash, Move& move) noexcept {
        // Binary search to find the FIRST entry with this hash
        int left = 0;
        int right = BOOK_SIZE - 1;
        int firstMatch = -1;

        // Find first occurrence
        while (left <= right) {
            int mid = left + (right - left) / 2;

            if (OPENING_BOOK[mid].Hash == hash) {
                firstMatch = mid;
                right = mid - 1;
            } else if (OPENING_BOOK[mid].Hash < hash) {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }

        // Not found
        if (firstMatch == -1) {
            return false;
        }

        // Count how many entries have this hash
        int count = 1;
        while (firstMatch + count < BOOK_SIZE && OPENING_BOOK[firstMatch + count].Hash == hash) {
            ++count;
        }

        // Randomly select one of the moves for this position
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, count - 1);

        const int selected = dist(rng);
        const int idx = firstMatch + selected;

        // Construct the move
        move = Move(
            OPENING_BOOK[idx].StartSquare,
            OPENING_BOOK[idx].TargetSquare,
            static_cast<MoveFlag>(OPENING_BOOK[idx].Flag)
        );

        return true;
    }
};

#pragma region Context

struct GameContext {
    static inline void BeginNew() {
        GameHistory.clear();
        MoveCount = 0;
    }

    static inline TranspositionTable TT;
    static inline std::vector<uint64_t> GameHistory;
    static inline uint16_t MoveCount;
};

namespace SearchLimits {
    constexpr inline int MaxPly = 256;
    constexpr inline int NoEval = 1 << 30;
    constexpr inline int ContKeys = 12 * Ranks * Files;
}

struct SearchThreadData {
    int32_t HistoryTable[2 * Ranks * Files * Ranks * Files]{};
    uint64_t PositionHistory[SearchLimits::MaxPly]{};
    Move killerMoves[SearchLimits::MaxPly][2];
    Move ExcludedMove[SearchLimits::MaxPly];
    PawnTranspositionTable PawnTT;

    // used to tell whether the position is "improving" compared to two plies ago
    int StaticEval[SearchLimits::MaxPly];
    int ContKey[SearchLimits::MaxPly];
    // the node at this ply was reached by a null move, so it must not null move again
    bool SkipNull[SearchLimits::MaxPly];

    SearchThreadData() {
        for (int i = 0; i < SearchLimits::MaxPly; ++i) {
            StaticEval[i] = SearchLimits::NoEval;
            ContKey[i] = -1;
            SkipNull[i] = false;
        }
    }

    std::unique_ptr<int16_t[]> ContHist = std::make_unique<int16_t[]>(
        static_cast<std::size_t>(SearchLimits::ContKeys) * SearchLimits::ContKeys
    );
};

struct SearchContext {
    // maximum ply that can be reached
    static constexpr inline const int MaxPly = SearchLimits::MaxPly;
    // maximum number of legal moves
    static constexpr inline const int MaxMoves = 256;
    // maximum number of ply extensions allowed
    static constexpr inline const int MaxExt = 6;
    static constexpr inline int MaxHistory = 16384;

    using ThreadData = SearchThreadData;

    static inline ThreadData MainData;
    static inline thread_local ThreadData* Tls = &MainData;

    [[nodiscard]]
    static inline ThreadData& Data() noexcept {
        return *Tls;
    }

    static inline void BeginNew(uint64_t currentHash) {
        ThreadData& d = Data();

        for (auto& h : d.HistoryTable) h /= 2;

        {
            int16_t* ch = d.ContHist.get();
            const std::size_t n = static_cast<std::size_t>(SearchLimits::ContKeys) * SearchLimits::ContKeys;
            for (std::size_t i = 0; i < n; ++i) ch[i] = static_cast<int16_t>(ch[i] / 2);
        }

        std::memset(d.killerMoves, 0, sizeof(d.killerMoves));
        std::memset(d.PositionHistory, 0, sizeof(d.PositionHistory));
        std::memset(d.ExcludedMove, 0, sizeof(d.ExcludedMove));

        for (int i = 0; i < SearchLimits::MaxPly; ++i) {
            d.StaticEval[i] = SearchLimits::NoEval;
            d.ContKey[i] = -1;
            d.SkipNull[i] = false;
        }

        d.PositionHistory[0] = currentHash;

        GameContext::TT.NewSearch();

        Cancelled.store(false, std::memory_order_relaxed);
        ResetStats();
    }

    static inline std::atomic<bool> Cancelled;

    [[nodiscard]]
    static constexpr inline int HistoryIndex(PieceColor color, int from, int to) noexcept {
        return (static_cast<int>(color) * (Ranks * Files) + from) * (Ranks * Files) + to;
    }

    static inline void UpdateHistory(PieceColor color, int from, int to, int bonus) noexcept {
        int32_t& h = Data().HistoryTable[HistoryIndex(color, from, to)];
        h += bonus - h * std::abs(bonus) / MaxHistory;
    }

    static inline void UpdateContHistory(int prevKey, int key, int bonus) noexcept {
        if (prevKey < 0) return;

        int16_t& h = Data().ContHist[static_cast<std::size_t>(prevKey) * SearchLimits::ContKeys + key];
        h = static_cast<int16_t>(h + bonus - h * std::abs(bonus) / MaxHistory);
    }

    [[nodiscard]]
    static inline int ContHistoryScore(int prevKey, int key) noexcept {
        if (prevKey < 0) return 0;
        return Data().ContHist[static_cast<std::size_t>(prevKey) * SearchLimits::ContKeys + key];
    }

    static inline bool CatchAllMode{false};
    static inline PieceColor EnginePlayer{PieceColor::White};

    static inline PieceColor RootSide{PieceColor::White};

    static inline std::atomic<uint64_t> Nodes{0};
    static inline std::atomic<int> SelDepth{0};

    static inline void ResetStats() {
        Nodes.store(0, std::memory_order_relaxed);
        SelDepth.store(0, std::memory_order_relaxed);
    }

    static inline void CountNode(int ply) noexcept {
        Nodes.fetch_add(1, std::memory_order_relaxed);

        int prevMax = SelDepth.load(std::memory_order_relaxed);
        while (ply > prevMax && !SelDepth.compare_exchange_weak(prevMax, ply, std::memory_order_relaxed));
    }
};

#pragma region Scoring

struct ScoredMove {
    Move Move;
    int Score;
};

constexpr int INF = 1'000'000;
constexpr int SearchCancelScore = 2'000'000;
constexpr int MateThreshold = INF - 512;

#pragma region Lookup Tables

namespace LUTs {
    // only diagonal captures
    static inline uint64_t PawnAttacks[2][64];
    // single and double pushes + attacks
    static inline uint64_t PawnMoves[2][64];
    // only attacks
    static inline uint64_t KingAttacks[64];
    // only attacks
    static inline uint64_t KnightAttacks[64];
    // squares strictly between a and b, 0 if not aligned
    static inline uint64_t RayBetween[64][64];
    // check masks for passed pawns
    static inline uint64_t PassedPawn[2][64];
    // pawn shield squares (immediate rank in front of king, 3 files wide)
    static inline uint64_t PawnShield[2][64];
    // pawn shield second rank
    static inline uint64_t PawnShield2[2][64];
    // precomputed reductions for LMR
    static inline int Reduction[SearchContext::MaxPly][SearchContext::MaxMoves];
    // pawns on the files next to a given file
    static inline uint64_t AdjacentFiles[8];
    // adjacent-file squares on the same rank or behind (own side), used to detect backward pawns
    static inline uint64_t BackSupport[2][64];
    // king zone: king square + ring + one extra rank towards the enemy
    static inline uint64_t KingZone[2][64];

    static inline void InitAttks() {
        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const int r = ToRank(sq);
            const int f = ToFile(sq);

            /* Pawn moves and attacks */
            for (const int col : {0, 1}) {
                uint64_t attacks = 0ull;
                uint64_t moves = 0ull;

                const int step = col ? Files : -Files;
                const int homeRank = col ? 1 : (Ranks - 2);

                if (col ? (r < Ranks - 1) : (r > 0)) {
                    // left capture
                    if (f > 0) attacks |= IndexToMask(sq + step - 1);
                    // right capture
                    if (f < Files - 1) attacks |= IndexToMask(sq + step + 1);

                    // single push
                    moves |= IndexToMask(sq + step);
                    // double push, only from home rank
                    if (r == homeRank) moves |= IndexToMask(sq + step + step);
                }

                PawnAttacks[col][sq] = attacks;
                PawnMoves[col][sq] = moves | attacks;
            }

            /* King attacks */ {
                uint64_t attacks = 0ull;

                for (int dr = -1; dr <= 1; ++dr) {
                    for (int df = -1; df <= 1; ++df) {
                        if (dr == 0 && df == 0) continue;

                        const int nr = r + dr;
                        const int nf = f + df;

                        if (nr >= 0 && nr < Ranks && nf >= 0 && nf < Files) {
                            attacks |= IndexToMask(To2DIndex(nr, nf));
                        }
                    }
                }

                KingAttacks[sq] = attacks;
            }

            /* Knight attacks */ {
                constexpr int KnightOffsets[8][2] = {
                    {-2, -1}, {-2, 1}, {-1, -2}, {-1, 2},
                    {1, -2}, {1, 2}, {2, -1}, {2, 1}
                };

                uint64_t attacks = 0ull;

                for (auto [dr, df] : KnightOffsets) {
                    const int nr = r + dr;
                    const int nf = f + df;

                    if (nr >= 0 && nr < Ranks && nf >= 0 && nf < Files) {
                        attacks |= IndexToMask(To2DIndex(nr, nf));
                    }
                }

                KnightAttacks[sq] = attacks;
            }
        }
    }

    static inline void InitRayTable() {
        for (int a = 0; a < 64; ++a) {
            for (int b = 0; b < 64; ++b) {
                if (a == b) continue;

                const int ar = ToRank(a);
                const int af = ToFile(a);
                const int br = ToRank(b);
                const int bf = ToFile(b);

                const int dr = (br > ar) - (br < ar);
                const int df = (bf > af) - (bf < af);

                const bool aligned = (dr == 0) || (df == 0) || (std::abs(br - ar) == std::abs(bf - af));
                if (!aligned) continue;

                uint64_t ray = 0ull;
                int r = ar + dr;
                int f = af + df;

                while (r != br || f != bf) {
                    ray |= IndexToMask(To2DIndex(r, f));
                    r += dr;
                    f += df;
                }

                RayBetween[a][b] = ray;
            }
        }
    }

    static inline void InitPassedPawnTable() {
        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const int r = ToRank(sq);
            const int f = ToFile(sq);

            for (const int col : {0, 1}) {
                uint64_t mask = 0ull;

                const int step = col ? 1 : -1;

                for (int nr = r + step; nr >= 0 && nr < Ranks; nr += step) {
                    for (int df = -1; df <= 1; ++df) {
                        const int nf = f + df;
                        if (nf >= 0 && nf < Files) mask |= IndexToMask(To2DIndex(nr, nf));
                    }
                }

                PassedPawn[col][sq] = mask;
            }
        }
    }

    static inline void InitPawnShieldTable() {
        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const int r = ToRank(sq);
            const int f = ToFile(sq);

            uint64_t shield1W = 0ull;
            uint64_t shield2W = 0ull;
            uint64_t shield1B = 0ull;
            uint64_t shield2B = 0ull;

            for (int df = -1; df <= 1; ++df) {
                const int nf = f + df;
                if (nf < 0 || nf >= Files) continue;

                // white shields
                if (r - 1 >= 0) shield1W |= IndexToMask(To2DIndex(r - 1, nf));
                if (r - 2 >= 0) shield2W |= IndexToMask(To2DIndex(r - 2, nf));

                // black shields
                if (r + 1 < Ranks) shield1B |= IndexToMask(To2DIndex(r + 1, nf));
                if (r + 2 < Ranks) shield2B |= IndexToMask(To2DIndex(r + 2, nf));
            }

            PawnShield[0][sq] = shield1W;
            PawnShield2[0][sq] = shield2W;
            PawnShield[1][sq] = shield1B;
            PawnShield2[1][sq] = shield2B;
        }
    }

    static inline void InitPawnHelperTables() {
        for (int f = 0; f < Files; ++f) {
            uint64_t m = 0ull;
            if (f > 0) m |= 0x0101010101010101ull << (f - 1);
            if (f < Files - 1) m |= 0x0101010101010101ull << (f + 1);
            AdjacentFiles[f] = m;
        }

        for (int sq = 0; sq < Ranks * Files; ++sq) {
            const int r = ToRank(sq);
            const int f = ToFile(sq);

            for (const int col : {0, 1}) {
                // "behind" for white = higher rank index, for black = lower rank index
                const int back = col ? -1 : 1;
                uint64_t support = 0ull;

                for (int nr = r; nr >= 0 && nr < Ranks; nr += back) {
                    for (const int df : {-1, 1}) {
                        const int nf = f + df;
                        if (nf >= 0 && nf < Files) support |= IndexToMask(To2DIndex(nr, nf));
                    }
                }

                BackSupport[col][sq] = support;

                // king zone: 3x3 around the king plus the rank in front of that (towards the enemy)
                const int fwd = col ? 1 : -1;
                uint64_t zone = KingAttacks[sq] | IndexToMask(sq);

                const int zr = r + 2 * fwd;
                if (zr >= 0 && zr < Ranks) {
                    for (int df = -1; df <= 1; ++df) {
                        const int nf = f + df;
                        if (nf >= 0 && nf < Files) zone |= IndexToMask(To2DIndex(zr, nf));
                    }
                }

                KingZone[col][sq] = zone;
            }
        }
    }

    static inline void InitLMRTable() {
        for (int p = 1; p < SearchContext::MaxPly; ++p) {
            for (int m = 1; m < SearchContext::MaxMoves; ++m) {
                Reduction[p][m] = std::max(0, int(0.5f + std::log(p) * std::log(m) * 0.5f));
            }
        }
    }

    static inline void Init() {
        InitAttks();
        InitRayTable();
        InitPassedPawnTable();
        InitPawnShieldTable();
        InitPawnHelperTables();
        InitLMRTable();
    }
}

void Chess::Init() {
    ZobristHasher::Init();
    MagicBitboard::Init();
    LUTs::Init();

    GameContext::TT.SetSize(128);
    SearchContext::MainData.PawnTT.SetSize(2);
}

void Chess::Board::NewGame() {
    GameContext::TT.Clear();
    SearchContext::MainData.PawnTT.Clear();

    std::memset(SearchContext::Data().HistoryTable, 0, sizeof(SearchContext::Data().HistoryTable));
    std::memset(
        SearchContext::Data().ContHist.get(), 0,
        sizeof(int16_t) * static_cast<std::size_t>(SearchLimits::ContKeys) * SearchLimits::ContKeys
    );
}

#pragma region Evaluation Weights

namespace {
    struct Score {
        int mg{0};
        int eg{0};

        constexpr Score() = default;
        constexpr Score(int m, int e) : mg(m), eg(e) {}

        constexpr Score& operator+=(const Score& o) noexcept {
            mg += o.mg; eg += o.eg; return *this;
        }
        constexpr Score& operator-=(const Score& o) noexcept {
            mg -= o.mg; eg -= o.eg; return *this;
        }
    };

    constexpr Score operator+(Score a, const Score& b) noexcept {
        return a += b;
    }

    constexpr Score operator-(Score a, const Score& b) noexcept {
        return a -= b;
    }

    constexpr Score operator*(const Score& a, int k) noexcept {
        return Score(a.mg * k, a.eg * k);
    }

    [[nodiscard]]
    constexpr inline int ChebDist(int a, int b) noexcept {
        return std::max(std::abs(ToRank(a) - ToRank(b)), std::abs(ToFile(a) - ToFile(b)));
    }

    [[nodiscard]]
    constexpr inline int ManhattanDist(int a, int b) noexcept {
        return std::abs(ToRank(a) - ToRank(b)) + std::abs(ToFile(a) - ToFile(b));
    }

    // Material (middlegame, endgame), indexed by piece type: pawn, bishop, knight, rook, queen, king
    constexpr Score PieceValue[] = {
        {100, 100}, {278, 365}, {288, 291}, {411, 621},
        {838, 1103}, {0, 0}
    };

    // Pawn structure
    constexpr Score Isolated{-1, -17};
    constexpr Score IsolatedOpen{-3, 16};
    constexpr Score Backward{-14, -11};
    constexpr Score BackwardOpen{-2, 40};
    constexpr Score Doubled{-12, 10};
    constexpr Score DoubledPassed{-12, -34};
    constexpr Score ConnectedPassers{-15, 20};

    // Pawn structure, indexed by how far the pawn has advanced (1 = starting rank)
    constexpr Score Supported[] = {
        {0, 0}, {0, 0}, {10, 22}, {6, 15},
        {26, 22}, {82, 12}, {141, -40}, {0, 0}
    };

    constexpr Score Phalanx[] = {
        {0, 0}, {1, 6}, {8, 10}, {8, 12},
        {4, 37}, {-33, 0}, {34, 38}, {0, 0}
    };

    constexpr Score Candidate[] = {
        {0, 0}, {0, 0}, {0, 0}, {-5, 50},
        {-3, 36}, {-58, 110}, {18, 45}, {0, 0}
    };

    // King pawn shield and open files in front of the king
    constexpr Score ShieldMissing1{-5, 7};
    constexpr Score ShieldMissing2{-2, -3};
    constexpr Score KingFileSemi{-3, -10};
    constexpr Score KingFileOpen{-30, -15};

    // Passed pawns: base value by advancement, bonus by file
    constexpr Score PassedBase[] = {
        {0, 0}, {28, 51}, {27, 86}, {24, 123},
        {48, 145}, {70, 147}, {3, 225}, {245, 490}
    };

    constexpr Score PassedFile[] = {
        {5, 4}, {24, 2}, {29, -8}, {-16, -3},
        {-14, -1}, {-26, -15}, {-71, 6}, {-11, 21}
    };

    // Passed pawns: blocked / path / king distance / rule of the square
    constexpr int PassedBlockedPct = 63;
    constexpr int PassedBehindPct = 73;
    constexpr int KingProxOpp = 19;
    constexpr int KingProxOwn = 3;
    constexpr int PathFree = 69;
    constexpr int PathClear = 43;
    constexpr int PathStop = 11;
    constexpr int PathDefended = 14;
    constexpr int PathDiv = 6;
    constexpr int UnstoppableEg = 230;

    // Mobility, indexed by the number of safe squares attacked
    constexpr Score KnightMobility[] = {
        {-8, -49}, {1, -17}, {8, -5}, {7, 22},
        {16, 16}, {14, 23}, {19, 26}, {25, 11},
        {33, 7}
    };

    constexpr Score BishopMobility[] = {
        {-7, -19}, {15, 16}, {16, 14}, {18, 36},
        {22, 27}, {21, 27}, {27, 17}, {22, 43},
        {30, 20}, {5, 37}, {33, 23}, {-1, 10},
        {27, 26}, {196, -41}
    };

    constexpr Score RookMobility[] = {
        {-21, 2}, {-8, 12}, {3, 22}, {4, 36},
        {1, 30}, {12, 19}, {11, 28}, {16, 33},
        {9, 43}, {7, 45}, {-1, 49}, {2, 46},
        {23, 38}, {13, 32}, {14, 36}
    };

    constexpr Score QueenMobility[] = {
        {32, -7}, {14, -3}, {11, -150}, {7, -69},
        {2, -28}, {3, -39}, {1, -40}, {3, 39},
        {6, 21}, {2, 17}, {7, 16}, {15, 21},
        {23, 31}, {23, 40}, {19, 60}, {22, 52},
        {27, 42}, {33, 29}, {1, 76}, {26, 30},
        {26, 16}, {29, 20}, {32, 5}, {31, -50},
        {33, 39}, {33, 40}, {34, 43}, {35, 45}
    };

    // Minor pieces
    constexpr Score BishopPair{45, 68};
    constexpr Score KnightFork{-14, 143};
    constexpr Score KnightOutpost{12, 23};
    constexpr Score KnightReach{12, 18};
    constexpr Score BishopOutpost{30, -11};
    constexpr Score BishopReach{7, 3};
    constexpr Score MinorBehind{3, -1};
    constexpr Score KingProtector{-4, 4};
    constexpr Score BadBishop{-2, -1};
    constexpr Score LongDiagonal{11, 13};

    // Rooks
    constexpr Score RookOpen{17, 19};
    constexpr Score RookSemi{1, 18};
    constexpr Score TrappedRook{-13, 7};
    constexpr Score RookOnPawn{-10, 3};
    constexpr Score RookBehindPasser{15, 12};

    // King safety: attack units per attacking piece / safe check, scaled by the number of attackers
    constexpr int UnitBishop = -2;
    constexpr int UnitKnight = 12;
    constexpr int UnitRook = 15;
    constexpr int UnitQueen = 45;
    constexpr int CheckKnight = 95;
    constexpr int CheckBishop = 85;
    constexpr int CheckRook = 204;
    constexpr int CheckQueen = 80;
    constexpr int AttackerScale[] = {24, 40, 62, 114, 144, 100, 100, 100, 100};
    constexpr int ZoneHitUnit = 1;
    constexpr int QueenlessPct = 69;
    constexpr int DangerCap = 205;
    constexpr int WeakRingUnit = 24;
    constexpr int KnightDefUnit = -39;
    constexpr int BishopDefUnit = -7;
    constexpr Score FlankAttack{3, -2};

    // Threats
    constexpr Score PawnAttackMinor{79, 96};
    constexpr Score PawnAttackRook{98, 20};
    constexpr Score PawnAttackQueen{58, 19};
    constexpr Score MinorAttackRook{59, 41};
    constexpr Score MinorAttackQueen{56, 15};
    constexpr Score RookAttackQueen{105, 47};
    constexpr Score WeakPawnByMinor{4, 9};
    constexpr Score WeakKnightByMinor{11, 10};
    constexpr Score WeakBishopByMinor{36, 19};
    constexpr Score WeakPawnByRook{3, 11};
    constexpr Score WeakMinorByRook{15, 25};
    constexpr Score WeakRookByRook{-7, -11};
    constexpr Score KingThreat{60, 28};
    constexpr Score HangingPiece{28, 20};
    constexpr Score HangingPawn{12, 19};
    constexpr Score Restricted{5, 8};
    constexpr Score PawnPush{16, 22};
    constexpr Score KnightOnQueen{18, 4};
    constexpr Score SliderOnQueen{20, -1};

    // Material imbalance: per pawn above / below five
    constexpr Score KnightPerPawn{11, 9};
    constexpr Score RookPerPawn{-4, -1};

    // Endgame scaling (out of 64) and tempo
    constexpr int ScaleNoPawnsWeakPawns = 19;
    constexpr int ScaleNoPawns = 31;
    constexpr int ScaleOcbLead = 21;
    constexpr int ScaleOcb = 29;
    constexpr int ScaleOcbRooks = 39;
    constexpr int ScaleBase = 42;
    constexpr int ScalePerPawn = 7;
    constexpr int Tempo = 27;
}

namespace PieceSquareTables {
    constexpr int16_t Table[12][Ranks * Files] = {
        // Pawn, middlegame
        {
            -46, -46, -46, -46, -46, -46, -46, -46,
            33, 68, 34, 98, 57, 73, -61, -86,
            -71, -45, -5, 4, 5, 28, -3, -44,
            -81, -53, -44, -39, -20, -29, -28, -47,
            -96, -60, -61, -35, -41, -52, -38, -67,
            -97, -66, -61, -64, -48, -60, -23, -51,
            -94, -60, -65, -76, -57, -28, 6, -48,
            -46, -46, -46, -46, -46, -46, -46, -46
        },
        // Bishop, middlegame
        {
            -55, -38, -88, -116, -116, -88, -38, -55,
            -1, 16, 26, -5, -5, 26, 16, -1,
            -11, 34, 50, 45, 45, 50, 34, -11,
            -1, 6, 45, 65, 65, 45, 6, -1,
            -16, 3, 5, 42, 42, 5, 3, -16,
            6, 2, 0, 9, 9, 0, 2, 6,
            4, 19, 15, -5, -5, 15, 19, 4,
            -18, -5, -27, -22, -22, -27, -5, -18
        },
        // Knight, middlegame
        {
            -80, -188, -162, -114, -114, -162, -188, -80,
            -18, -13, 43, 22, 22, 43, -13, -18,
            -1, 11, 45, 43, 43, 45, 11, -1,
            0, -9, 21, 23, 23, 21, -9, 0,
            -20, -5, -3, 2, 2, -3, -5, -20,
            -40, -25, -20, -10, -10, -20, -25, -40,
            -32, -36, -26, -19, -19, -26, -36, -32,
            -82, -50, -53, -49, -49, -53, -50, -82
        },
        // Rook, middlegame
        {
            -11, 7, -11, -2, 2, 33, 44, 58,
            -46, -40, -43, -18, -56, -14, -13, 46,
            -52, -21, -47, -29, -28, -9, 46, -1,
            -44, -17, -37, -15, -34, -20, -17, -12,
            -76, -57, -74, -53, -69, -72, -54, -66,
            -91, -60, -82, -73, -85, -75, -44, -60,
            -101, -70, -84, -77, -92, -75, -59, -100,
            -79, -60, -78, -53, -76, -72, -81, -102
        },
        // Queen, middlegame
        {
            -67, -28, 17, 8, 8, 17, -28, -67,
            -7, -88, -48, -105, -105, -48, -88, -7,
            -39, -37, -30, -47, -47, -30, -37, -39,
            -29, -44, -29, -40, -40, -29, -44, -29,
            -49, -61, -52, -55, -55, -52, -61, -49,
            -60, -62, -62, -67, -67, -62, -62, -60,
            -40, -57, -41, -51, -51, -41, -57, -40,
            -89, -101, -75, -65, -65, -75, -101, -89
        },
        // King, middlegame
        {
            257, 189, 229, 192, 196, 227, 217, 247,
            66, -2, 38, 1, 5, 36, 26, 56,
            -49, -117, -77, -114, -110, -79, -89, -59,
            24, -44, -4, -41, -37, -6, -16, 14,
            -14, -82, -42, -79, -75, -44, -54, -24,
            25, -43, -3, -15, -11, -5, -15, 15,
            74, 31, 21, 9, 13, 19, 59, 64,
            86, 68, 58, 21, 25, 56, 96, 76
        },
        // Pawn, endgame
        {
            -90, -90, -90, -90, -90, -90, -90, -90,
            67, 60, 56, 2, 2, 10, 66, 86,
            18, 22, -10, -34, -39, -57, -14, 1,
            -45, -56, -76, -89, -91, -94, -78, -59,
            -67, -70, -88, -94, -90, -94, -85, -74,
            -77, -75, -93, -82, -82, -91, -85, -80,
            -68, -66, -75, -69, -64, -78, -81, -77,
            -90, -90, -90, -90, -90, -90, -90, -90
        },
        // Bishop, endgame
        {
            13, 30, 36, 80, 80, 36, 30, 13,
            -5, 56, 35, 69, 69, 35, 56, -5,
            45, -1, 35, 24, 24, 35, -1, 45,
            8, 59, 26, 69, 69, 26, 59, 8,
            -6, 43, 71, 58, 58, 71, 43, -6,
            -9, 15, 42, 61, 61, 42, 15, -9,
            -28, -3, -7, 48, 48, -7, -3, -28,
            -37, 6, -4, 46, 46, -4, 6, -37
        },
        // Knight, endgame
        {
            -43, 38, 37, 49, 49, 37, 38, -43,
            34, 46, 33, 59, 59, 33, 46, 34,
            28, 35, 48, 58, 58, 48, 35, 28,
            49, 74, 79, 98, 98, 79, 74, 49,
            62, 66, 87, 106, 106, 87, 66, 62,
            44, 56, 63, 97, 97, 63, 56, 44,
            30, 36, 44, 55, 55, 44, 36, 30,
            8, -20, 23, 37, 37, 23, -20, 8
        },
        // Rook, endgame
        {
            16, 12, 32, 43, 43, 32, 12, 16,
            -10, 56, 58, 53, 53, 58, 56, -10,
            43, 23, 35, 46, 46, 35, 23, 43,
            32, 10, 24, 27, 27, 24, 10, 32,
            27, 11, 35, 28, 28, 35, 11, 27,
            16, -18, 12, 39, 39, 12, -18, 16,
            19, -25, 1, 16, 16, 1, -25, 19,
            -36, 4, 18, 21, 21, 18, 4, -36
        },
        // Queen, endgame
        {
            6, 26, -5, 21, 21, -5, 26, 6,
            36, 129, 90, 147, 147, 90, 129, 36,
            27, 64, 61, 105, 105, 61, 64, 27,
            9, 65, 32, 82, 82, 32, 65, 9,
            -1, 53, 16, 62, 62, 16, 53, -1,
            -12, 26, 12, 36, 36, 12, 26, -12,
            -71, -16, -53, -5, -5, -53, -16, -71,
            -50, -12, -41, -18, -18, -41, -12, -50
        },
        // King, endgame
        {
            -28, 16, 26, 42, 45, 16, 7, -14,
            8, 53, 63, 78, 81, 53, 44, 22,
            -31, 36, 91, 106, 109, 81, 27, -17,
            -72, -5, 72, 87, 90, 62, -14, -58,
            -80, -13, 64, 79, 82, 54, -22, -66,
            -79, -12, 43, 58, 61, 33, -21, -65,
            -61, -39, 16, 9, 12, 6, -48, -47,
            -109, -43, -55, -62, -59, -65, -52, -95
        }
    };

    [[nodiscard]]
    static constexpr inline int PstMg(int pieceTypeIdx, int sq, bool isBlack) noexcept {
        return Table[pieceTypeIdx][isBlack ? Utils::Mirror(sq) : sq];
    }

    [[nodiscard]]
    static constexpr inline int PstEg(int pieceTypeIdx, int sq, bool isBlack) noexcept {
        return Table[pieceTypeIdx + 6][isBlack ? Utils::Mirror(sq) : sq];
    }
}

#pragma region Incremental State

namespace IncrementalEval {
    constexpr int PhaseWeight[6] = {0, 1, 1, 2, 4, 0};

    static inline void Update(int& mg, int& eg, int& phase, PieceType type, bool black, int sq, int sign) noexcept {
        if (type == PieceType::Pawn || type == PieceType::King) return;

        const int t = static_cast<int>(type);
        const int s = black ? -sign : sign;

        mg += s * (PieceValue[t].mg + PieceSquareTables::PstMg(t, sq, black));
        eg += s * (PieceValue[t].eg + PieceSquareTables::PstEg(t, sq, black));

        phase += sign * PhaseWeight[t];
    }
}

void Board::refreshDerivedState() {
    m_ColorOcc[0] = 0ull;
    m_ColorOcc[1] = 0ull;

    for (int i = 0; i < 6; ++i) {
        m_ColorOcc[0] |= m_PieceMask[i];
        m_ColorOcc[1] |= m_PieceMask[i + 6];
    }

    m_IncMg = 0;
    m_IncEg = 0;
    m_Phase = 0;

    for (int c = 0; c < 2; ++c) {
        // bishop, knight, rook, queen
        for (int t = 1; t <= 4; ++t) {
            for (uint64_t mask = m_PieceMask[c * 6 + t]; mask; mask &= mask - 1) {
                IncrementalEval::Update(m_IncMg, m_IncEg, m_Phase, static_cast<PieceType>(t), c == 1, MaskToIndex(mask), 1);
            }
        }
    }
}

#pragma region Fen String

PieceColor Board::LoadFromFen(const std::string& fen) {
    const std::vector<std::string> tokens = Utils::Tokenize(fen);

    // validate token
    if (tokens.size() != 6) {
        std::cerr << "Invalid fen string" << std::endl;
        return PieceColor::White;
    }

    GameContext::BeginNew();

    // refer all tokens
    const std::string& pieceStr = tokens[0];
    const std::string& sideToMoveStr = tokens[1];
    const std::string& castlingRightsStr = tokens[2];
    const std::string& enPassantSquareStr = tokens[3];
    const std::string& halfMoveClockStr = tokens[4];
    const std::string& moveCountStr = tokens[5];

    /* Parse pieces */ {
        // reset
        std::memset(m_PieceMask, 0ull, sizeof(m_PieceMask));
        for (Piece& p : m_Mailbox) p.Type = PieceType::None;

        int idx = 0;

        for (const char c : pieceStr) {
            // safety check
            if (idx >= Ranks * Files) break;

            // line break
            if (c == '/') continue;

            // skip empty
            if (std::isdigit(c)) {
                idx += c - '0';
                continue;
            }

            const int lower_c = std::tolower(c);

            PieceType type;
            const bool isBlack = c == lower_c;
            const int colorOff = isBlack * 6;

            // get type
            switch (lower_c) {
            case 'p': type = PieceType::Pawn; break;
            case 'b': type = PieceType::Bishop; break;
            case 'n': type = PieceType::Knight; break;
            case 'q': type = PieceType::Queen; break;
            case 'r': type = PieceType::Rook; break;
            case 'k': type = PieceType::King; break;
            default: continue;
            }

            m_Mailbox[idx] = Piece(type, isBlack ? PieceColor::Black : PieceColor::White);
            m_PieceMask[static_cast<int>(type) + colorOff] |= IndexToMask(idx++);
        }
    }

    // Parse side to move
    const PieceColor sideToMove = sideToMoveStr[0] == 'w' ? PieceColor::White : PieceColor::Black;

    /* Parse castling */ {
        if (castlingRightsStr[0] == '-') {
            m_CastlingRights = 0;
        } else {
            m_CastlingRights = (
                (CastlingRights::WhiteKingSideBit * (castlingRightsStr.find('K') != std::string::npos)) |
                (CastlingRights::WhiteQueenSideBit * (castlingRightsStr.find('Q') != std::string::npos)) |
                (CastlingRights::BlackKingSideBit * (castlingRightsStr.find('k') != std::string::npos)) |
                (CastlingRights::BlackQueenSideBit * (castlingRightsStr.find('q') != std::string::npos))
            );
        }
    }

    /* Parse En-Passant square */ {
        if (enPassantSquareStr[0] == '-') {
            m_EnPassantSquare = NullPos;
        } else {
            m_EnPassantSquare = StringNotationToIndex(enPassantSquareStr[0], enPassantSquareStr[1]);
        }
    }

    // Parse half-move clock
    m_HalfMoveClock = std::stoi(halfMoveClockStr);

    // Parse full move count (number of times black has moved)
    GameContext::MoveCount = std::stoi(moveCountStr);

    refreshDerivedState();

    m_Hash = ZobristHasher::GetHash(*this, sideToMove);
    m_PawnAndKingHash = ZobristHasher::GetPawnAndKingHash(*this);

    // push current position in history
    GameContext::GameHistory.push_back(m_Hash);

    return sideToMove;
}

std::string Chess::Board::GetFen(PieceColor sideToMove) const {
    const char sep = ' ';

    std::string fen;
    fen.reserve(80); // average length of a fen string

    /* Serialize pieces */ {
        for (int rank = 0; rank < Ranks; ++rank) {
            int empty = 0;

            for (int file = 0; file < Files; ++file) {
                const int idx = To2DIndex(rank, file);
                const Piece& p = m_Mailbox[idx];

                if (p.Type == PieceType::None) {
                    ++empty;
                    continue;
                }

                if (empty) {
                    fen.push_back('0' + empty);
                    empty = 0;
                }

                fen.push_back(PieceToChar(p));
            }

            if (empty) fen.push_back('0' + empty);
            if (rank != Ranks - 1) fen.push_back('/');
        }
    }

    // side to move
    fen.push_back(sep);
    fen.push_back(sideToMove == PieceColor::White ? 'w' : 'b');

    /* Serialize Castling rights */ {
        fen.push_back(sep);

        if (!m_CastlingRights) {
            fen.push_back('-');
        } else {
            if (m_CastlingRights & CastlingRights::WhiteKingSideBit) fen.push_back('K');
            if (m_CastlingRights & CastlingRights::WhiteQueenSideBit) fen.push_back('Q');
            if (m_CastlingRights & CastlingRights::BlackKingSideBit) fen.push_back('k');
            if (m_CastlingRights & CastlingRights::BlackQueenSideBit) fen.push_back('q');
        }
    }

    /* Serialize En-passant square */ {
        fen.push_back(sep);

        if (m_EnPassantSquare == NullPos) {
            fen.push_back('-');
        } else {
            fen.push_back('a' + ToFile(m_EnPassantSquare));
            fen.push_back('1' + (Ranks - 1 - ToRank(m_EnPassantSquare)));
        }
    }

    // half move clock
    fen.push_back(sep);
    fen += std::to_string(m_HalfMoveClock);

    // full move count
    fen.push_back(sep);
    fen += std::to_string(GameContext::MoveCount);

    return fen;
}

#pragma region Gameplay

bool Board::isUnderAttack(int position, PieceColor attacker) const {
    const bool isBlack = attacker == PieceColor::Black;
    const int atkOff = isBlack ? 6 : 0;

    const uint64_t occ = GetOccupancyMap();

    return (
        // pawn attacks
        (LUTs::PawnAttacks[!isBlack][position] & m_PieceMask[atkOff]) ||
        // knight attacks
        (LUTs::KnightAttacks[position] & m_PieceMask[atkOff + 2]) ||
        // king attacks
        (LUTs::KingAttacks[position] & m_PieceMask[atkOff + 5]) ||
        // bishop / queen attacks
        (MagicBitboard::DiagonalAttacks(position, occ) & (m_PieceMask[atkOff + 1] | m_PieceMask[atkOff + 4])) ||
        // rook / queen attacks
        (MagicBitboard::OrthogonalAttacks(position, occ) & (m_PieceMask[atkOff + 3] | m_PieceMask[atkOff + 4]))
    );
}

bool Board::isUnderCheck(PieceColor sideToMove) const {
    const int kingSq = MaskToIndex(m_PieceMask[Utils::PieceToBitIdx(PieceType::King, sideToMove)]);
    return isUnderAttack(kingSq, InvertColor(sideToMove));
}

#pragma region Forced Draw

bool Board::FiftyMoveRule() const noexcept {
    return m_HalfMoveClock >= 100;
}

bool Board::isThreefoldRepetition() const noexcept {
    int count = 1;

    const int histSize = GameContext::GameHistory.size();
    const int lookback = std::min<int>(histSize - 1, m_HalfMoveClock);

    for (int i = histSize - 2; i >= histSize - 1 - lookback; --i) {
        if (GameContext::GameHistory[i] == m_Hash && ++count >= 3) return true;
    }

    return false;
}

bool Board::isThreefoldRepetition(int ply) const {
    int count = 1;

    // check within current search
    for (int i = ply - 2; i >= 0; i -= 2) {
        // intentionally used threshold of 2 here instead of 3 to avoid repetitions in search
        // with the value of 3, the engine sometimes did get stuck in a repetition loop
        if (SearchContext::Data().PositionHistory[i] == m_Hash && ++count >= 2) return true;
    }

    // check game history
    return isThreefoldRepetition();
}

bool Board::HasInsufficientMaterial() const noexcept {
    // any pawns, rooks or queens = mating material always possible
    if (
        m_PieceMask[0] || m_PieceMask[6] ||
        m_PieceMask[3] || m_PieceMask[9] ||
        m_PieceMask[4] || m_PieceMask[10]
    ) {
        return false;
    }

    const uint64_t whiteBishops = m_PieceMask[1];
    const uint64_t blackBishops = m_PieceMask[7];
    const uint64_t whiteKnights = m_PieceMask[2];
    const uint64_t blackKnights = m_PieceMask[8];

    const int whiteBishopCount = NumBitsOn(whiteBishops);
    const int blackBishopCount = NumBitsOn(blackBishops);
    const int whiteKnightCount = NumBitsOn(whiteKnights);
    const int blackKnightCount = NumBitsOn(blackKnights);

    const int whiteMinors = whiteBishopCount + whiteKnightCount;
    const int blackMinors = blackBishopCount + blackKnightCount;

    // K vs K
    if (whiteMinors == 0 && blackMinors == 0) return true;

    // K+minor vs K
    if (whiteMinors == 1 && blackMinors == 0) return true;
    if (blackMinors == 1 && whiteMinors == 0) return true;

    // K+N+N vs K - cannot force checkmate
    if (whiteKnightCount == 2 && blackMinors == 0 && whiteBishopCount == 0) return true;
    if (blackKnightCount == 2 && whiteMinors == 0 && blackBishopCount == 0) return true;

    // K+N vs K+N - neither side has mating potential
    if (
        whiteKnightCount == 1 && blackKnightCount == 1 &&
        whiteBishopCount == 0 && blackBishopCount == 0
    ) {
        return true;
    }

    // K+B vs K+B - only drawn if bishops are on same color
    if (
        whiteBishopCount == 1 && blackBishopCount == 1 &&
        whiteKnightCount == 0 && blackKnightCount == 0
    ) {
        // light square = 1, dark square = 0
        // a square is light if (rank + file) is even
        const auto IsLightSquare = [](int sq) -> bool { return (ToRank(sq) + ToFile(sq)) % 2 == 0; };
        if (IsLightSquare(MaskToIndex(whiteBishops)) == IsLightSquare(MaskToIndex(blackBishops))) return true;
    }

    // K+B* vs K+B* where ALL bishops across both sides are on the same color
    // e.g. white has 2 light bishops, black has 1 light bishop - still a draw
    if (whiteKnightCount == 0 && blackKnightCount == 0 &&
        whiteBishopCount >= 1 && blackBishopCount >= 1) {
        // light squares = (rank + file) even, represented by this mask
        constexpr uint64_t LightSquares = 0x55AA55AA55AA55AAull;

        const bool whiteAllLight = (whiteBishops & ~LightSquares) == 0;
        const bool whiteAllDark = (whiteBishops & LightSquares) == 0;
        const bool blackAllLight = (blackBishops & ~LightSquares) == 0;
        const bool blackAllDark = (blackBishops & LightSquares) == 0;

        // all bishops on the board are on the same color square
        if ((whiteAllLight && blackAllLight) || (whiteAllDark && blackAllDark)) return true;
    }

    return false;
}

bool Board::HasAnyLegalMoves(PieceColor sideToMove) const noexcept {
    const bool isBlack = sideToMove == PieceColor::Black;
    const PieceColor enemyColor = InvertColor(sideToMove);

    const int myOff = isBlack ? 6 : 0;
    const int enmOff = myOff ^ 6;

    const uint64_t friendly = GetOccupancyMap(sideToMove);
    const uint64_t enemy = GetOccupancyMap(enemyColor);
    const uint64_t occ = friendly | enemy;

    const PinMasks pins = getPinMasks(sideToMove, friendly, occ);
    const uint64_t checkMask = getCheckMask(sideToMove, occ);

    /* king */ {
        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);
        const uint64_t attacked = getAttackedSquares(enemyColor, occ);

        if (LUTs::KingAttacks[kingSq] & ~friendly & ~attacked) return true;
    }

    // only king moves are legal in double checks
    if (checkMask == 0ull) return false;

    /* pawns */ {
        const int pushDir = isBlack ? Files : -Files;
        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

        for (uint64_t tmp = m_PieceMask[myOff]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;

            // captures
            if (LUTs::PawnAttacks[isBlack][sq] & enemy & allowed) return true;

            // en-passant
            if (m_EnPassantSquare != NullPos) {
                if (LUTs::PawnAttacks[isBlack][sq] & IndexToMask(m_EnPassantSquare)) {
                    const int capturedSq = m_EnPassantSquare - pushDir;
                    const uint64_t epMask = IndexToMask(m_EnPassantSquare) | IndexToMask(capturedSq);

                    // must either capture checker or block check, and must respect a pin
                    const bool epResolvesCheck = (epMask & checkMask) != 0ull;
                    const bool epRespectsPin = !(pins.Pinned & IndexToMask(sq)) || (pins.Rays[sq] & IndexToMask(m_EnPassantSquare));

                    if (epResolvesCheck && epRespectsPin) {
                        const uint64_t occAfterEP = (occ & ~IndexToMask(sq) & ~IndexToMask(capturedSq)) | IndexToMask(m_EnPassantSquare);
                        const uint64_t enemyRQ = m_PieceMask[enmOff + 3] | m_PieceMask[enmOff + 4];

                        if (!(MagicBitboard::OrthogonalAttacks(kingSq, occAfterEP) & enemyRQ)) return true;
                    }
                }
            }

            // pushes: single square must be empty first
            if (!(occ & IndexToMask(sq + pushDir))) {
                if (IndexToMask(sq + pushDir) & allowed) return true;

                // double push, only from the home rank
                const uint64_t homeRank = isBlack ? 0x000000000000FF00ull : 0x00FF000000000000ull;

                if (IndexToMask(sq) & homeRank) {
                    const uint64_t to2mask = IndexToMask(sq + pushDir + pushDir);
                    if (!(occ & to2mask) && (to2mask & allowed)) return true;
                }
            }
        }
    }

    // knights
    for (uint64_t tmp = m_PieceMask[myOff + 2] & ~pins.Pinned; tmp; tmp &= tmp - 1) {
        if (LUTs::KnightAttacks[MaskToIndex(tmp)] & ~friendly & checkMask) return true;
    }

    // bishops
    for (uint64_t tmp = m_PieceMask[myOff + 1]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        if (MagicBitboard::DiagonalAttacks(sq, occ) & ~friendly & allowed) return true;
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[myOff + 3]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        if (MagicBitboard::OrthogonalAttacks(sq, occ) & ~friendly & allowed) return true;
    }

    // queens
    for (uint64_t tmp = m_PieceMask[myOff + 4]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        if (MagicBitboard::CombinedAttacks(sq, occ) & ~friendly & allowed) return true;
    }

    return false;
}

#pragma region Move

void Board::doMoveInternal(Move move) {
    const uint64_t fromMask = IndexToMask(move.StartingSquare);
    const uint64_t toMask = IndexToMask(move.TargetSquare);

    const Piece mover = m_Mailbox[move.StartingSquare];
    const bool moverIsBlack = mover.Color == PieceColor::Black;

    const int srcColorOff = moverIsBlack ? 6 : 0;
    const int srcIdx = static_cast<int>(mover.Type) + srcColorOff;

    const int myColor = moverIsBlack ? 1 : 0;
    const int enemyColor = myColor ^ 1;

    const bool isCapture = HasFlag(move.Flag, MoveFlag::Capture);

    // XOR out old piece
    m_Hash = ZobristHasher::HashPiece(m_Hash, mover, move.StartingSquare);

    if (mover.Type == PieceType::Pawn || mover.Type == PieceType::King) {
        m_PawnAndKingHash = ZobristHasher::HashPiece(m_PawnAndKingHash, mover, move.StartingSquare);
    }

    // XOR out captured piece
    if (isCapture && !HasFlag(move.Flag, MoveFlag::EnPassant)) {
        const Piece captured = m_Mailbox[move.TargetSquare];
        const int capIdx = Utils::PieceToBitIdx(captured.Type, captured.Color);

        m_PieceMask[capIdx] &= ~toMask;

        m_ColorOcc[enemyColor] &= ~toMask;
        IncrementalEval::Update(m_IncMg, m_IncEg, m_Phase, captured.Type, !moverIsBlack, move.TargetSquare, -1);

        m_Hash = ZobristHasher::HashPiece(m_Hash, captured, move.TargetSquare);

        if (captured.Type == PieceType::Pawn) {
            m_PawnAndKingHash = ZobristHasher::HashPiece(m_PawnAndKingHash, captured, move.TargetSquare);
        }
    }

    // flip side to move
    m_Hash = ZobristHasher::HashSideToMove(m_Hash);

    // remove old en-passant square from hash
    if (m_EnPassantSquare != NullPos) {
        m_Hash = ZobristHasher::HashEnPassantSquare(m_Hash, m_EnPassantSquare);
    }

    if (m_CastlingRights) {
        // to track whether castling rights have actually changed
        const uint8_t prevCastlingRights = m_CastlingRights;

        if (mover.Type == PieceType::King) {
            // king moved
            if (moverIsBlack) {
                m_CastlingRights &= ~(CastlingRights::BlackKingSideBit | CastlingRights::BlackQueenSideBit);
            } else {
                m_CastlingRights &= ~(CastlingRights::WhiteKingSideBit | CastlingRights::WhiteQueenSideBit);
            }
        } else if (mover.Type == PieceType::Rook) {
            // rook leaves its home square
            if (move.StartingSquare == 63) {
                m_CastlingRights &= ~CastlingRights::WhiteKingSideBit;
            } else if (move.StartingSquare == 56) {
                m_CastlingRights &= ~CastlingRights::WhiteQueenSideBit;
            } else if (move.StartingSquare == 7) {
                m_CastlingRights &= ~CastlingRights::BlackKingSideBit;
            } else if (move.StartingSquare == 0) {
                m_CastlingRights &= ~CastlingRights::BlackQueenSideBit;
            }
        }

        if (isCapture && !HasFlag(move.Flag, MoveFlag::EnPassant)) {
            // a rook on its home square gets captured
            if (move.TargetSquare == 63) {
                m_CastlingRights &= ~CastlingRights::WhiteKingSideBit;
            } else if (move.TargetSquare == 56) {
                m_CastlingRights &= ~CastlingRights::WhiteQueenSideBit;
            } else if (move.TargetSquare == 7) {
                m_CastlingRights &= ~CastlingRights::BlackKingSideBit;
            } else if (move.TargetSquare == 0) {
                m_CastlingRights &= ~CastlingRights::BlackQueenSideBit;
            }
        }

        if (m_CastlingRights != prevCastlingRights) {
            // XOR out last rights
            m_Hash = ZobristHasher::HashCastlingRights(m_Hash, prevCastlingRights);

            // XOR in new rights
            m_Hash = ZobristHasher::HashCastlingRights(m_Hash, m_CastlingRights);
        }
    }

    // remove piece from starting square
    m_PieceMask[srcIdx] &= ~fromMask;

    m_ColorOcc[myColor] = (m_ColorOcc[myColor] & ~fromMask) | toMask;
    IncrementalEval::Update(m_IncMg, m_IncEg, m_Phase, mover.Type, moverIsBlack, move.StartingSquare, -1);

    // move pieces in mailbox
    m_Mailbox[move.TargetSquare].Color = mover.Color; // type is set below after promotions
    m_Mailbox[move.StartingSquare].Type = PieceType::None;

    // reset en-passant square (will be set below if required)
    m_EnPassantSquare = NullPos;

    // 50-move rule
    if (
        mover.Type == PieceType::Pawn ||
        isCapture
    ) {
        m_HalfMoveClock = 0;
    } else {
        ++m_HalfMoveClock;
    }

    // handle move flags
    PieceType landingType = mover.Type;

    if (isCastle(move.Flag)) {
        const bool isKingside = HasFlag(move.Flag, MoveFlag::CastleKingSide);
        const int dir = isKingside ? 1 : -1;
        const int rookOffset = isKingside ? 3 : 4;
        const int rookFrom = move.StartingSquare + rookOffset * dir;
        const int rookTo = move.TargetSquare - dir;

        const int rookIdx = 3 + srcColorOff;
        const Piece rook(PieceType::Rook, mover.Color);

        m_Hash = ZobristHasher::HashPiece(m_Hash, rook, rookFrom);

        m_PieceMask[rookIdx] &= ~IndexToMask(rookFrom);
        m_PieceMask[rookIdx] |= IndexToMask(rookTo);

        m_ColorOcc[myColor] = (m_ColorOcc[myColor] & ~IndexToMask(rookFrom)) | IndexToMask(rookTo);
        IncrementalEval::Update(m_IncMg, m_IncEg, m_Phase, PieceType::Rook, moverIsBlack, rookFrom, -1);
        IncrementalEval::Update(m_IncMg, m_IncEg, m_Phase, PieceType::Rook, moverIsBlack, rookTo, 1);

        m_Mailbox[rookTo] = rook;
        m_Mailbox[rookFrom].Type = PieceType::None;

        m_Hash = ZobristHasher::HashPiece(m_Hash, rook, rookTo);
    }

    else if (HasFlag(move.Flag, MoveFlag::PawnDoublePush)) {
        const int dir = moverIsBlack ? Files : -Files;

        m_EnPassantSquare = move.StartingSquare + dir;
        m_Hash = ZobristHasher::HashEnPassantSquare(m_Hash, m_EnPassantSquare);
    }

    else if (HasFlag(move.Flag, MoveFlag::EnPassant)) {
        const int dir = moverIsBlack ? Files : -Files;
        const Piece enemyPawn(PieceType::Pawn, InvertColor(mover.Color));
        const int enemyPawnIdx = (!moverIsBlack) * 6;
        const int capturedPos = move.TargetSquare - dir;

        m_Hash = ZobristHasher::HashPiece(m_Hash, enemyPawn, capturedPos);
        m_PawnAndKingHash = ZobristHasher::HashPiece(m_PawnAndKingHash, enemyPawn, capturedPos);

        m_PieceMask[enemyPawnIdx] &= ~IndexToMask(capturedPos);
        m_ColorOcc[enemyColor] &= ~IndexToMask(capturedPos);
        m_Mailbox[capturedPos].Type = PieceType::None;
    }

    else if (isPromotion(move.Flag)) {
        if (HasFlag(move.Flag, MoveFlag::PromoteToQueen)) landingType = PieceType::Queen;
        else if (HasFlag(move.Flag, MoveFlag::PromoteToRook)) landingType = PieceType::Rook;
        else if (HasFlag(move.Flag, MoveFlag::PromoteToKnight)) landingType = PieceType::Knight;
        else landingType = PieceType::Bishop;
    }

    // set piece at new position
    m_PieceMask[static_cast<int>(landingType) + srcColorOff] |= toMask;
    m_Mailbox[move.TargetSquare].Type = landingType;

    IncrementalEval::Update(m_IncMg, m_IncEg, m_Phase, landingType, moverIsBlack, move.TargetSquare, 1);

    const Piece lander = Piece(landingType, mover.Color);

    // XOR in the moved piece at its destination
    m_Hash = ZobristHasher::HashPiece(m_Hash, lander, move.TargetSquare);

    if (landingType == PieceType::Pawn || landingType == PieceType::King) {
        m_PawnAndKingHash = ZobristHasher::HashPiece(m_PawnAndKingHash, lander, move.TargetSquare);
    }
}

void Board::DoMove(Move move) {
    doMoveInternal(move);

    GameContext::GameHistory.push_back(m_Hash);
    ++GameContext::MoveCount;
}

void Chess::Board::FlipSideToMove() {
    m_Hash = ZobristHasher::HashSideToMove(m_Hash);
}

#pragma region Move Generation

void Chess::Board::scanPins(int kingPos, uint64_t pinners, uint64_t friendlyOcc, PinMasks& result) const {
    for (uint64_t tmp = pinners; tmp; tmp &= tmp - 1) {
        const int attSq = MaskToIndex(tmp);
        const uint64_t between = LUTs::RayBetween[kingPos][attSq];
        const uint64_t blocker = between & friendlyOcc;

        if (NumBitsOn(blocker) != 1) continue;

        const int pinnedSq = MaskToIndex(blocker);

        result.Pinned |= IndexToMask(pinnedSq);
        result.Rays[pinnedSq] = between | IndexToMask(attSq);
    }
}

Board::PinMasks Board::getPinMasks(PieceColor friendlyColor, uint64_t friendlyOcc, uint64_t totalOcc) const {
    PinMasks result;

    const int enemyOff = friendlyColor == PieceColor::Black ? 0 : 6;

    const int kingSq = MaskToIndex(m_PieceMask[Utils::PieceToBitIdx(PieceType::King, friendlyColor)]);

    const uint64_t rookBlockers = MagicBitboard::OrthogonalAttacks(kingSq, totalOcc) & friendlyOcc;
    const uint64_t rookXray = MagicBitboard::OrthogonalAttacks(kingSq, totalOcc ^ rookBlockers);
    const uint64_t enemyRooksQueens = (
        m_PieceMask[3 + enemyOff] |
        m_PieceMask[4 + enemyOff]
    );

    scanPins(kingSq, rookXray & enemyRooksQueens, friendlyOcc, result);

    const uint64_t bishBlockers = MagicBitboard::DiagonalAttacks(kingSq, totalOcc) & friendlyOcc;
    const uint64_t bishXray = MagicBitboard::DiagonalAttacks(kingSq, totalOcc ^ bishBlockers);
    const uint64_t enemyBishopsQueens = (
        m_PieceMask[1 + enemyOff] |
        m_PieceMask[4 + enemyOff]
    );

    scanPins(kingSq, bishXray & enemyBishopsQueens, friendlyOcc, result);

    return result;
}

Board::PinMasks Board::getPinMasks(PieceColor friendlyColor) const {
    const uint64_t friendly = GetOccupancyMap(friendlyColor);
    const uint64_t occ = GetOccupancyMap();

    return getPinMasks(friendlyColor, friendly, occ);
}

uint64_t Board::getCheckMask(PieceColor friendlyColor, uint64_t occ) const {
    const int enemyOff = (friendlyColor == PieceColor::White) * 6;
    const bool isBlack = friendlyColor == PieceColor::Black;
    const int kingSq = MaskToIndex(m_PieceMask[Utils::PieceToBitIdx(PieceType::King, friendlyColor)]);

    uint64_t checkMask = 0ull;
    int checkerCount = 0;

    // sliders
    const uint64_t enemyQueens = m_PieceMask[enemyOff + 4];
    const uint64_t enemyRooksQueens = m_PieceMask[enemyOff + 3] | enemyQueens;
    const uint64_t enemyBishopsQueens = m_PieceMask[enemyOff + 1] | enemyQueens;

    for (uint64_t tmp = MagicBitboard::OrthogonalAttacks(kingSq, occ) & enemyRooksQueens; tmp; tmp &= tmp - 1) {
        const int attSq = MaskToIndex(tmp);
        checkMask |= LUTs::RayBetween[kingSq][attSq] | IndexToMask(attSq);
        if (++checkerCount == 2) return 0ull; // double check, only king can move
    }

    for (uint64_t tmp = MagicBitboard::DiagonalAttacks(kingSq, occ) & enemyBishopsQueens; tmp; tmp &= tmp - 1) {
        const int attSq = MaskToIndex(tmp);
        checkMask |= LUTs::RayBetween[kingSq][attSq] | IndexToMask(attSq);
        if (++checkerCount == 2) return 0ull; // double check, only king can move
    }

    // knights
    for (uint64_t tmp = LUTs::KnightAttacks[kingSq] & m_PieceMask[enemyOff + 2]; tmp; tmp &= tmp - 1) {
        checkMask |= IndexToMask(MaskToIndex(tmp));
        if (++checkerCount == 2) return 0ull; // double check, only king can move
    }

    // pawns
    for (uint64_t tmp = LUTs::PawnAttacks[isBlack][kingSq] & m_PieceMask[enemyOff]; tmp; tmp &= tmp - 1) {
        checkMask |= IndexToMask(MaskToIndex(tmp));
        if (++checkerCount == 2) return 0ull; // double check, only king can move
    }

    if (checkerCount == 0) return ~0ull;  // not in check, all squares valid

    return checkMask;
}

uint64_t Board::getCheckMask(PieceColor friendlyColor) const {
    return getCheckMask(friendlyColor, GetOccupancyMap());
}

uint64_t Board::getAttackedSquares(PieceColor attacker, uint64_t occ) const {
    uint64_t attacked = 0ull;

    const uint64_t occxking = occ & ~m_PieceMask[Utils::PieceToBitIdx(PieceType::King, InvertColor(attacker))];

    const int off = attacker == PieceColor::Black ? 6 : 0;
    const uint64_t pawns = m_PieceMask[off];

    // pawn
    if (attacker == PieceColor::White) {
        attacked |= ((pawns & ~0x0101010101010101ull) >> 9) | ((pawns & ~0x8080808080808080ull) >> 7);
    } else {
        attacked |= ((pawns & ~0x8080808080808080ull) << 9) | ((pawns & ~0x0101010101010101ull) << 7);
    }

    // knights
    for (uint64_t tmp = m_PieceMask[off + 2]; tmp; tmp &= tmp - 1) {
        attacked |= LUTs::KnightAttacks[MaskToIndex(tmp)];
    }

    // bishops
    for (uint64_t tmp = m_PieceMask[off + 1]; tmp; tmp &= tmp - 1) {
        attacked |= MagicBitboard::DiagonalAttacks(MaskToIndex(tmp), occxking);
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[off + 3]; tmp; tmp &= tmp - 1) {
        attacked |= MagicBitboard::OrthogonalAttacks(MaskToIndex(tmp), occxking);
    }

    // queens
    for (uint64_t tmp = m_PieceMask[off + 4]; tmp; tmp &= tmp - 1) {
        attacked |= MagicBitboard::CombinedAttacks(MaskToIndex(tmp), occxking);
    }

    // king
    attacked |= LUTs::KingAttacks[MaskToIndex(m_PieceMask[off + 5])];

    return attacked;
}

uint64_t Board::getAttackedSquares(PieceColor attacker) const {
    return getAttackedSquares(attacker, GetOccupancyMap());
}

Board::CheckDetector Chess::Board::buildCheckDetector(bool attackerIsBlack, int enemyKingSq, uint64_t occ, uint64_t friendlyOcc) const {
    const int myOff = attackerIsBlack ? 6 : 0;

    CheckDetector cd;
    cd.KingSq = enemyKingSq;
    cd.KingMask = IndexToMask(enemyKingSq);

    // reverse lookup: squares from which our pawn/knight attacks their king
    cd.PawnChecks = LUTs::PawnAttacks[!attackerIsBlack][enemyKingSq];
    cd.KnightChecks = LUTs::KnightAttacks[enemyKingSq];

    // dc candidates: our pieces sitting on a xray line between one of our
    // sliders and the enemy king - moving them may reveal an attack
    const uint64_t ourBQ = m_PieceMask[myOff + 1] | m_PieceMask[myOff + 4];
    const uint64_t ourRQ = m_PieceMask[myOff + 3] | m_PieceMask[myOff + 4];

    uint64_t dc = 0ull;

    const uint64_t diagRays = MagicBitboard::DiagonalAttacks(enemyKingSq, occ);
    const uint64_t diagBlock = diagRays & friendlyOcc;
    if (diagBlock && (MagicBitboard::DiagonalAttacks(enemyKingSq, occ ^ diagBlock) & ourBQ)) dc |= diagBlock;

    const uint64_t orthRays = MagicBitboard::OrthogonalAttacks(enemyKingSq, occ);
    const uint64_t orthBlock = orthRays & friendlyOcc;
    if (orthBlock && (MagicBitboard::OrthogonalAttacks(enemyKingSq, occ ^ orthBlock) & ourRQ)) dc |= orthBlock;

    cd.DcCandidates = dc;
    return cd;
}

bool Board::moveGivesCheck(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd) const {
    const int myOff = isBlack ? 6 : 0;

    const uint64_t fromMask = IndexToMask(move.StartingSquare);
    const uint64_t toMask = IndexToMask(move.TargetSquare);

    PieceType landingType = m_Mailbox[move.StartingSquare].Type;
    if (isPromotion(move.Flag)) landingType = Utils::PromotionPiece(move.Flag);

    const bool isEnPassant = HasFlag(move.Flag, MoveFlag::EnPassant);

    const bool isDC = isEnPassant || (fromMask & cd.DcCandidates);

    // non-sliders: exact via precomputed table, 0 magic lookups
    if (landingType == PieceType::Pawn && (toMask & cd.PawnChecks)) return true;
    if (landingType == PieceType::Knight && (toMask & cd.KnightChecks)) return true;

    // non-slider with no dc potential - done, no magic lookups at all
    if ((landingType == PieceType::Pawn || landingType == PieceType::Knight) && !isDC) return false;

    // build new occupancy once - shared by direct slider check + dc check
    uint64_t newOcc = (occ & ~fromMask) | toMask;

    if (isEnPassant) {
        const int pushDir = isBlack ? Files : -Files;
        newOcc &= ~IndexToMask(move.TargetSquare - pushDir);
    }

    // slider direct check - handles all edge cases (piece blocking its own ray)
    // because we use newOcc which correctly reflects the move
    if (landingType == PieceType::Bishop || landingType == PieceType::Queen)
        if (MagicBitboard::DiagonalAttacks(move.TargetSquare, newOcc) & cd.KingMask) return true;

    if (landingType == PieceType::Rook || landingType == PieceType::Queen)
        if (MagicBitboard::OrthogonalAttacks(move.TargetSquare, newOcc) & cd.KingMask) return true;

    // castle: rook slides to new square, check that
    if (landingType == PieceType::King && isCastle(move.Flag)) {
        const bool isKingside = HasFlag(move.Flag, MoveFlag::CastleKingSide);
        const int rookFromSq = isKingside ? move.StartingSquare + 3 : move.StartingSquare - 4;
        const int rookToSq = isKingside ? move.TargetSquare - 1 : move.TargetSquare + 1;
        const uint64_t cOcc = (newOcc & ~IndexToMask(rookFromSq)) | IndexToMask(rookToSq);
        if (MagicBitboard::OrthogonalAttacks(rookToSq, cOcc) & cd.KingMask) return true;
    }

    // discovered check - only paid for dc candidates (~rare)
    if (isDC) {
        const uint64_t myBQ = (m_PieceMask[myOff + 1] | m_PieceMask[myOff + 4]) & ~fromMask;
        const uint64_t myRQ = (m_PieceMask[myOff + 3] | m_PieceMask[myOff + 4]) & ~fromMask;
        if (MagicBitboard::DiagonalAttacks(cd.KingSq, newOcc) & myBQ) return true;
        if (MagicBitboard::OrthogonalAttacks(cd.KingSq, newOcc) & myRQ) return true;
    }

    return false;
}

void Board::pushMove(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd, Move* out, unsigned int& moveCount) const {
    if (moveGivesCheck(move, isBlack, occ, cd)) {
        move.Flag = AddFlag(move.Flag, MoveFlag::Check);
    }

    out[moveCount++] = move;
}

void Board::pushPromotions(Move move, bool isBlack, uint64_t occ, const CheckDetector& cd, Move* out, unsigned int& moveCount) const {
    pushMove(Move(move.StartingSquare, move.TargetSquare, AddFlag(move.Flag, MoveFlag::PromoteToQueen)), isBlack, occ, cd, out, moveCount);
    pushMove(Move(move.StartingSquare, move.TargetSquare, AddFlag(move.Flag, MoveFlag::PromoteToRook)), isBlack, occ, cd, out, moveCount);
    pushMove(Move(move.StartingSquare, move.TargetSquare, AddFlag(move.Flag, MoveFlag::PromoteToKnight)), isBlack, occ, cd, out, moveCount);
    pushMove(Move(move.StartingSquare, move.TargetSquare, AddFlag(move.Flag, MoveFlag::PromoteToBishop)), isBlack, occ, cd, out, moveCount);
}

void Board::generateMoves(
    PieceColor sideToMove, const PinMasks& pins,
    uint64_t checkMask, uint64_t attacked, uint64_t friendlyOcc,
    uint64_t enemyOcc, uint64_t occ,
    int filterIdx,
    Move* out, unsigned int& moveCount
) const {
    const bool isBlack = sideToMove == PieceColor::Black;
    const int myOff = isBlack ? 6 : 0;
    const int enmOff = myOff ^ 6;

    const int enemyKingSq = MaskToIndex(m_PieceMask[enmOff + 5]);
    const CheckDetector cd = buildCheckDetector(isBlack, enemyKingSq, occ, friendlyOcc);

    static const auto ShouldProcess = [](int sq, int filterIdx) -> bool { return filterIdx < 0 || sq == filterIdx; };

    /* king */ {
        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

        if (ShouldProcess(kingSq, filterIdx)) {
            for (uint64_t tmp = LUTs::KingAttacks[kingSq] & ~friendlyOcc & ~attacked; tmp; tmp &= tmp - 1) {
                const int to = MaskToIndex(tmp);
                pushMove(Move(kingSq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
            }

            if (checkMask == ~0ull) {
                const int ksRight = isBlack ? CastlingRights::BlackKingSideBit : CastlingRights::WhiteKingSideBit;
                const int qsRight = isBlack ? CastlingRights::BlackQueenSideBit : CastlingRights::WhiteQueenSideBit;

                // kingside
                if (
                    // has right to castle
                    (m_CastlingRights & ksRight) &&
                    // f and g are empty
                    !(occ & (IndexToMask(kingSq + 1) | IndexToMask(kingSq + 2))) &&
                    // f and g aren't under attack
                    !(attacked & (IndexToMask(kingSq + 1) | IndexToMask(kingSq + 2)))
                ) {
                    pushMove(Move(kingSq, kingSq + 2, MoveFlag::CastleKingSide), isBlack, occ, cd, out, moveCount);
                }

                // queenside
                if (
                    // has right to castle
                    (m_CastlingRights & qsRight) &&
                    // b, c and d are empty
                    !(occ & (IndexToMask(kingSq - 1) | IndexToMask(kingSq - 2) | IndexToMask(kingSq - 3))) &&
                    // c and d aren't under attack
                    !(attacked & (IndexToMask(kingSq - 1) | IndexToMask(kingSq - 2)))
                ) {
                    pushMove(Move(kingSq, kingSq - 2, MoveFlag::CastleQueenSide), isBlack, occ, cd, out, moveCount);
                }
            }
        }
    }

    // in double check, only king moves are legal
    if (checkMask == 0ull) return;

    /* pawns */ {
        const int pushDir = isBlack ? Files : -Files;
        const int promoRankMin = isBlack ? 56 : 0;
        const int promoRankMax = isBlack ? 63 : 7;
        const uint64_t homeRank = isBlack ? 0x000000000000FF00ull : 0x00FF000000000000ull;

        const auto IsPromoSq = [&](int sq) { return sq >= promoRankMin && sq <= promoRankMax; };

        for (uint64_t tmp = m_PieceMask[myOff]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            if (!ShouldProcess(sq, filterIdx)) continue;

            const uint64_t sqMask = IndexToMask(sq);
            const uint64_t allowed = ((pins.Pinned & sqMask) ? pins.Rays[sq] : ~0ull) & checkMask;
            const uint64_t diag = LUTs::PawnAttacks[isBlack][sq];

            // captures
            for (uint64_t cap = diag & allowed & enemyOcc; cap; cap &= cap - 1) {
                const int to = MaskToIndex(cap);

                if (IsPromoSq(to)) pushPromotions(Move(sq, to, MoveFlag::Capture), isBlack, occ, cd, out, moveCount);
                else pushMove(Move(sq, to, MoveFlag::Capture), isBlack, occ, cd, out, moveCount);
            }

            // en-passant
            if (m_EnPassantSquare != NullPos) {
                if (diag & IndexToMask(m_EnPassantSquare)) {
                    const int capturedSq = m_EnPassantSquare - pushDir;
                    const uint64_t epMask = IndexToMask(m_EnPassantSquare) | IndexToMask(capturedSq);

                    // must capture checker or block check
                    const bool epResolvesCheck = (epMask & checkMask) != 0ull;

                    // pin restriction only
                    const bool epRespectsPin = !(pins.Pinned & sqMask) || (pins.Rays[sq] & IndexToMask(m_EnPassantSquare));

                    // a pinned pawn that can't resolve check via en-passant may still have a legal push below
                    if (epResolvesCheck && epRespectsPin) {
                        const uint64_t occAfterEP = (occ & ~sqMask & ~IndexToMask(capturedSq)) | IndexToMask(m_EnPassantSquare);
                        const uint64_t enemyRQ = m_PieceMask[enmOff + 3] | m_PieceMask[enmOff + 4];

                        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

                        // EP horizontal discovered check test
                        if (!(MagicBitboard::OrthogonalAttacks(kingSq, occAfterEP) & enemyRQ)) {
                            pushMove(Move(sq, m_EnPassantSquare, AddFlag(MoveFlag::EnPassant, MoveFlag::Capture)), isBlack, occ, cd, out, moveCount);
                        }
                    }
                }
            }

            // single push
            const int to1 = sq + pushDir;
            const uint64_t to1mask = IndexToMask(to1);

            if (!(occ & to1mask)) {
                if (to1mask & allowed) {
                    if (IsPromoSq(to1)) pushPromotions(Move(sq, to1, MoveFlag::None), isBlack, occ, cd, out, moveCount);
                    else pushMove(Move(sq, to1, MoveFlag::None), isBlack, occ, cd, out, moveCount);
                }

                // double push from home rank
                if (sqMask & homeRank) {
                    const int to2 = to1 + pushDir;
                    const uint64_t to2mask = IndexToMask(to2);

                    if (!(occ & to2mask) && (to2mask & allowed)) {
                        pushMove(Move(sq, to2, MoveFlag::PawnDoublePush), isBlack, occ, cd, out, moveCount);
                    }
                }
            }
        }
    }

    // knights
    for (uint64_t tmp = m_PieceMask[myOff + 2] & ~pins.Pinned; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        if (!ShouldProcess(sq, filterIdx)) continue;

        for (uint64_t mv = LUTs::KnightAttacks[sq] & ~friendlyOcc & checkMask; mv; mv &= mv - 1) {
            const int to = MaskToIndex(mv);
            pushMove(Move(sq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
        }
    }

    // bishops
    for (uint64_t tmp = m_PieceMask[myOff + 1]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        if (!ShouldProcess(sq, filterIdx)) continue;

        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        for (uint64_t mv = MagicBitboard::DiagonalAttacks(sq, occ) & ~friendlyOcc & allowed; mv; mv &= mv - 1) {
            const int to = MaskToIndex(mv);
            pushMove(Move(sq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
        }
    }

    // rooks
    for (uint64_t tmp = m_PieceMask[myOff + 3]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        if (!ShouldProcess(sq, filterIdx)) continue;

        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        for (uint64_t mv = MagicBitboard::OrthogonalAttacks(sq, occ) & ~friendlyOcc & allowed; mv; mv &= mv - 1) {
            const int to = MaskToIndex(mv);
            pushMove(Move(sq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
        }
    }

    // queens
    for (uint64_t tmp = m_PieceMask[myOff + 4]; tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);
        if (!ShouldProcess(sq, filterIdx)) continue;

        const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;
        for (uint64_t mv = MagicBitboard::CombinedAttacks(sq, occ) & ~friendlyOcc & allowed; mv; mv &= mv - 1) {
            const int to = MaskToIndex(mv);
            pushMove(Move(sq, to, (enemyOcc & IndexToMask(to)) ? MoveFlag::Capture : MoveFlag::None), isBlack, occ, cd, out, moveCount);
        }
    }
}

void Board::getAllLegalMoves(PieceColor sideToMove, Move* out, unsigned int& moveCount) const {
    getAllLegalMoves(sideToMove, getCheckMask(sideToMove, GetOccupancyMap()), out, moveCount);
}

void Board::getAllLegalMoves(PieceColor sideToMove, uint64_t checkMask, Move* out, unsigned int& moveCount) const {
    const uint64_t friendly = GetOccupancyMap(sideToMove);
    const uint64_t enemy = GetOccupancyMap(InvertColor(sideToMove));
    const uint64_t occ = friendly | enemy;

    const PinMasks pins = getPinMasks(sideToMove, friendly, occ);
    const uint64_t attacked = getAttackedSquares(InvertColor(sideToMove), occ);

    generateMoves(sideToMove, pins, checkMask, attacked, friendly, enemy, occ, -1, out, moveCount);
}

void Chess::Board::getAllCaptureMoves(PieceColor sideToMove, Move* out, unsigned int& moveCount) const {
    getAllCaptureMoves(sideToMove, getCheckMask(sideToMove, GetOccupancyMap()), out, moveCount);
}

void Chess::Board::getAllCaptureMoves(PieceColor sideToMove, uint64_t checkMask, Move* out, unsigned int& moveCount) const {
    const bool isBlack = sideToMove == PieceColor::Black;
    const int myOff = isBlack ? 6 : 0;
    const int enmOff = myOff ^ 6;

    const uint64_t friendly = GetOccupancyMap(sideToMove);
    const uint64_t enemy = GetOccupancyMap(InvertColor(sideToMove));
    const uint64_t occ = friendly | enemy;

    const PinMasks pins = getPinMasks(sideToMove, friendly, occ);

    /* king captures */ {
        const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

        // actually has something to capture
        if (uint64_t kingCaptures = LUTs::KingAttacks[kingSq] & enemy) {
            kingCaptures &= ~getAttackedSquares(InvertColor(sideToMove), occ);

            for (; kingCaptures; kingCaptures &= kingCaptures - 1) {
                out[moveCount++] = Move(kingSq, MaskToIndex(kingCaptures), MoveFlag::Capture);
            }
        }
    }

    // double check - only king moves are legal
    if (checkMask == 0ull) return;

    /* pawn captures + en-passant + promotion captures */ {
        const int pushDir = isBlack ? Files : -Files;
        const int promoRankMin = isBlack ? 56 : 0;
        const int promoRankMax = isBlack ? 63 : 7;

        const auto IsPromoSq = [&](int sq) { return sq >= promoRankMin && sq <= promoRankMax; };

        for (uint64_t tmp = m_PieceMask[myOff]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t sqMask = IndexToMask(sq);
            const uint64_t allowed = ((pins.Pinned & sqMask) ? pins.Rays[sq] : ~0ull) & checkMask;
            const uint64_t diag = LUTs::PawnAttacks[isBlack][sq];

            // captures
            for (uint64_t cap = diag & allowed & enemy; cap; cap &= cap - 1) {
                const int to = MaskToIndex(cap);

                if (IsPromoSq(to)) {
                    out[moveCount++] = Move(sq, to, AddFlag(MoveFlag::Capture, MoveFlag::PromoteToQueen));
                    out[moveCount++] = Move(sq, to, AddFlag(MoveFlag::Capture, MoveFlag::PromoteToRook));
                    out[moveCount++] = Move(sq, to, AddFlag(MoveFlag::Capture, MoveFlag::PromoteToKnight));
                    out[moveCount++] = Move(sq, to, AddFlag(MoveFlag::Capture, MoveFlag::PromoteToBishop));
                } else {
                    out[moveCount++] = Move(sq, to, MoveFlag::Capture);
                }
            }

            // en-passant
            if (m_EnPassantSquare != NullPos && (diag & IndexToMask(m_EnPassantSquare))) {
                const int capturedSq = m_EnPassantSquare - pushDir;
                const uint64_t epMask = IndexToMask(m_EnPassantSquare) | IndexToMask(capturedSq);

                // must either capture the checker or block the check ray
                if ((epMask & checkMask) == 0ull) continue;

                // respect pin ray
                if ((pins.Pinned & sqMask) && !(pins.Rays[sq] & IndexToMask(m_EnPassantSquare))) continue;

                // horizontal discovered check test - two pawns leave the rank at once
                // (the capturing pawn lands on the en-passant square, which can block a ray as well)
                const uint64_t occAfterEP = (occ & ~sqMask & ~IndexToMask(capturedSq)) | IndexToMask(m_EnPassantSquare);
                const uint64_t enemyRQ = m_PieceMask[enmOff + 3] | m_PieceMask[enmOff + 4];
                const int kingSq = MaskToIndex(m_PieceMask[myOff + 5]);

                if (!(MagicBitboard::OrthogonalAttacks(kingSq, occAfterEP) & enemyRQ)) {
                    out[moveCount++] = Move(sq, m_EnPassantSquare, AddFlag(MoveFlag::EnPassant, MoveFlag::Capture));
                }
            }
        }
    }

    /* knight captures */ {
        for (uint64_t tmp = m_PieceMask[myOff + 2] & ~pins.Pinned; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);

            for (uint64_t mv = LUTs::KnightAttacks[sq] & enemy & checkMask; mv; mv &= mv - 1) {
                out[moveCount++] = Move(sq, MaskToIndex(mv), MoveFlag::Capture);
            }
        }
    }

    /* bishop captures */ {
        for (uint64_t tmp = m_PieceMask[myOff + 1]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;

            for (uint64_t mv = MagicBitboard::DiagonalAttacks(sq, occ) & enemy & allowed; mv; mv &= mv - 1) {
                out[moveCount++] = Move(sq, MaskToIndex(mv), MoveFlag::Capture);
            }
        }
    }

    /* rook captures */ {
        for (uint64_t tmp = m_PieceMask[myOff + 3]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;

            for (uint64_t mv = MagicBitboard::OrthogonalAttacks(sq, occ) & enemy & allowed; mv; mv &= mv - 1) {
                out[moveCount++] = Move(sq, MaskToIndex(mv), MoveFlag::Capture);
            }
        }
    }

    /* queen captures */ {
        for (uint64_t tmp = m_PieceMask[myOff + 4]; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const uint64_t allowed = ((pins.Pinned & IndexToMask(sq)) ? pins.Rays[sq] : ~0ull) & checkMask;

            for (uint64_t mv = MagicBitboard::CombinedAttacks(sq, occ) & enemy & allowed; mv; mv &= mv - 1) {
                out[moveCount++] = Move(sq, MaskToIndex(mv), MoveFlag::Capture);
            }
        }
    }
}

std::vector<Move> Board::GetLegalMoves(int idx) const {
    const Piece piece = m_Mailbox[idx];
    if (piece.Type == PieceType::None) return {};

    const PieceColor enemy = InvertColor(piece.Color);
    const uint64_t friendly = GetOccupancyMap(piece.Color);
    const uint64_t enemyOcc = GetOccupancyMap(enemy);
    const uint64_t occ = friendly | enemyOcc;

    const PinMasks pins = getPinMasks(piece.Color, friendly, occ);
    const uint64_t checkMask = getCheckMask(piece.Color, occ);
    const uint64_t attacked = getAttackedSquares(enemy, occ);

    UninitArray<Move, MaxLegalMoves> moves;
    unsigned int numMoves = 0u;

    generateMoves(piece.Color, pins, checkMask, attacked, friendly, enemyOcc, occ, idx, moves, numMoves);

    return std::vector<Move>(moves.data(), moves.data() + numMoves);
}

#pragma region Occupancy

uint64_t Board::GetOccupancyMap(PieceColor color) const noexcept {
    return m_ColorOcc[static_cast<int>(color)];
}

uint64_t Board::GetOccupancyMap() const noexcept {
    return m_ColorOcc[0] | m_ColorOcc[1];
}

void Chess::Board::SetContempt(int contempt) const {
    Options::Contempt = contempt;
}

void Chess::Board::SetThreads(unsigned int threads) const {
    Options::Threads = static_cast<int>(std::clamp<unsigned int>(threads, 1u, 128u));
}

unsigned int Chess::Board::GetThreads() const noexcept {
    return static_cast<unsigned int>(Options::Threads);
}

void Chess::Board::SetHashSize(unsigned int size) const {
    GameContext::TT.SetSize(size);
    SearchContext::MainData.PawnTT.SetSize(std::max<int>(1, size / 64));
}

void Chess::Board::ClearHash() const {
    GameContext::TT.Clear();
    SearchContext::MainData.PawnTT.Clear();
}

unsigned int Chess::Board::GetHashSize() const noexcept {
    return GameContext::TT.GetSize();
}

#pragma region Evaluation

float Board::getEndgameWeight() const {
    // using weighted phase rather than linear as trading off queens or rooks is more
    // "endgameish" than bishops or knights

    constexpr int QueenWeight = 4;
    constexpr int RookWeight = 2;
    constexpr int MinorWeight = 1; // knights + bishops

    constexpr int MaxPhase = 2 * QueenWeight + 4 * RookWeight + 8 * MinorWeight;

    static_assert(IncrementalEval::PhaseWeight[4] == QueenWeight);
    static_assert(IncrementalEval::PhaseWeight[3] == RookWeight);
    static_assert(IncrementalEval::PhaseWeight[1] == MinorWeight && IncrementalEval::PhaseWeight[2] == MinorWeight);

    return 1.f - std::clamp(static_cast<float>(m_Phase) / MaxPhase, 0.f, 1.f);
}

#pragma region Eval Helpers

namespace {

    constexpr uint64_t FileA = 0x0101010101010101ull;
    constexpr uint64_t FileH = 0x8080808080808080ull;

    constexpr uint64_t LightSquaresMask = 0xAA55AA55AA55AA55ull;
    constexpr uint64_t CenterFilesMask = 0x3C3C3C3C3C3C3C3Cull;
    constexpr uint64_t CenterSquares = (1ull << 27) | (1ull << 28) | (1ull << 35) | (1ull << 36); // d5 e5 d4 e4

    constexpr uint64_t OutpostRanks[2] = {0x000000FFFFFF0000ull, 0x0000FFFFFF000000ull};
    constexpr uint64_t KingCamp[2] = {~0x0000000000FFFFFFull, 0x000000FFFFFFFFFFull};
    constexpr uint64_t KingFlank[8] = {
        0x0707070707070707ull, 0x0F0F0F0F0F0F0F0Full, 0x0F0F0F0F0F0F0F0Full, 0x3C3C3C3C3C3C3C3Cull,
        0x3C3C3C3C3C3C3C3Cull, 0xF0F0F0F0F0F0F0F0ull, 0xF0F0F0F0F0F0F0F0ull, 0xE0E0E0E0E0E0E0E0ull
    };

    [[nodiscard]]
    inline Score MobilityScore(int pt, int n) noexcept {
        switch (pt) {
        case 0: return KnightMobility[n];
        case 1: return BishopMobility[n];
        case 2: return RookMobility[n];
        default: return QueenMobility[n];
        }
    }

    [[nodiscard]]
    inline int NonPawnMaterial(const uint64_t* pm, int c) noexcept {
        return (
            300 * (NumBitsOn(pm[c * 6 + 1]) + NumBitsOn(pm[c * 6 + 2])) +
            500 * NumBitsOn(pm[c * 6 + 3]) + 900 * NumBitsOn(pm[c * 6 + 4])
        );
    }

    [[nodiscard]]
    constexpr inline bool IsLightSquare(int sq) noexcept {
        return ((ToRank(sq) + ToFile(sq)) & 1) == 0;
    }

    [[nodiscard]]
    constexpr inline int Advance(int c, int sq) noexcept {
        return c ? ToRank(sq) : (Ranks - 1 - ToRank(sq));
    }

    [[nodiscard]]
    constexpr inline uint64_t Forward(int c, uint64_t bb) noexcept {
        return c ? (bb << 8) : (bb >> 8);
    }

    [[nodiscard]]
    constexpr inline uint64_t ForwardFill(int c, uint64_t b) noexcept {
        if (c) {
            b |= b << 8; b |= b << 16; b |= b << 32;
        } else {
            b |= b >> 8; b |= b >> 16; b |= b >> 32;
        }

        return b;
    }

    [[nodiscard]]
    constexpr inline uint64_t PawnAttacksBB(int c, uint64_t pawns) noexcept {
        return (
            c ? (((pawns & ~FileA) << 7) | ((pawns & ~FileH) << 9))
            : (((pawns & ~FileA) >> 9) | ((pawns & ~FileH) >> 7))
        );
    }

    [[nodiscard]]
    constexpr inline uint64_t PawnDoubleAttacksBB(int c, uint64_t pawns) noexcept {
        return (
            c ? (((pawns & ~FileA) << 7) & ((pawns & ~FileH) << 9))
            : (((pawns & ~FileA) >> 9) & ((pawns & ~FileH) >> 7))
        );
    }

    Score EvalPawnsAndKing(const Board& b, int c, uint64_t& passedOut) noexcept {
        const bool black = c == 1;
        const int off = c * 6;
        const int e = c ^ 1;

        const uint64_t pawns = b.GetPieceMasks()[off];
        const uint64_t enemyPawns = b.GetPieceMasks()[e * 6];

        Score s;
        int fileCounts[Files] = {};
        int passedOnFile[Files] = {};
        uint64_t passed = 0ull;

        for (uint64_t tmp = pawns; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const int f = ToFile(sq);
            const int r = ToRank(sq);
            const int adv = black ? r : (Ranks - 1 - r);

            ++fileCounts[f];

            // material + piece square table
            s += PieceValue[0] + Score(PieceSquareTables::PstMg(0, sq, black), PieceSquareTables::PstEg(0, sq, black));

            const bool isPassed = !(LUTs::PassedPawn[c][sq] & enemyPawns);
            if (isPassed) {
                passed |= IndexToMask(sq);
                ++passedOnFile[f];
            }

            const uint64_t frontFile = LUTs::PassedPawn[c][sq] & Utils::FileMask(f);
            const bool opposed = (frontFile & enemyPawns) != 0;
            const bool isolated = !(LUTs::AdjacentFiles[f] & pawns);

            const uint64_t sameRankNeighbours =
                ((f > 0) ? IndexToMask(sq - 1) : 0ull) | ((f < Files - 1) ? IndexToMask(sq + 1) : 0ull);

            const bool supported = (LUTs::PawnAttacks[e][sq] & pawns) != 0;
            const bool phalanx = (sameRankNeighbours & pawns) != 0;

            if (isolated) {
                s += Isolated;
                if (!opposed) s += IsolatedOpen; // isolated on a half open file is a target
            } else if (!(LUTs::BackSupport[c][sq] & pawns)) {
                const int stop = sq + (black ? Files : -Files);
                if (LUTs::PawnAttacks[c][stop] & enemyPawns) {
                    s += Backward;
                    if (!opposed) s += BackwardOpen;
                }
            }

            if (supported) s += Supported[adv];
            if (phalanx) s += Phalanx[adv];

            {
                // a pawn that is not passed yet, but has at least as many helpers as there are enemy
                // pawns that could stop it, so a pawn break can turn it into a passer
                if (!isPassed && !opposed && adv >= 3) {
                    const int sentries = NumBitsOn(LUTs::PassedPawn[c][sq] & LUTs::AdjacentFiles[f] & enemyPawns);
                    const int helpers = NumBitsOn(LUTs::BackSupport[c][sq] & pawns);

                    if (sentries >= 1 && helpers >= sentries) s += Candidate[adv];
                }
            }

            if (isPassed) s += PassedFile[f];
        }

        for (int f = 0; f < Files; ++f) {
            const int count = fileCounts[f];
            if (count < 2) continue;

            const int doubled = count - 1;
            const int passedDoubled = std::min(doubled, passedOnFile[f]);
            const int normalDoubled = doubled - passedDoubled;

            s += Doubled * normalDoubled + DoubledPassed * passedDoubled;
        }

        // connected passed pawns
        for (int f = 0; f < Files; ++f) {
            if (!passedOnFile[f]) continue;
            const bool leftPassed = (f > 0) && passedOnFile[f - 1];
            const bool rightPassed = (f < Files - 1) && passedOnFile[f + 1];

            if (leftPassed || rightPassed) s += ConnectedPassers;
        }

        /* king: piece square table + pawn shield + open files in front of it */ {
            const int kingSq = MaskToIndex(b.GetPieceMasks()[off + 5]);

            s += Score(PieceSquareTables::PstMg(5, kingSq, black), PieceSquareTables::PstEg(5, kingSq, black));

            const int missing1 = NumBitsOn(LUTs::PawnShield[c][kingSq] & ~pawns);
            const int missing2 = NumBitsOn(LUTs::PawnShield2[c][kingSq] & ~pawns);

            s += ShieldMissing1 * missing1 + ShieldMissing2 * missing2;

            const int kf = ToFile(kingSq);
            for (int f = std::max(0, kf - 1); f <= std::min(Files - 1, kf + 1); ++f) {
                const uint64_t fm = Utils::FileMask(f);
                if (!(fm & pawns)) s += (fm & enemyPawns) ? KingFileSemi : KingFileOpen;
            }
        }

        passedOut = passed;
        return s;
    }

    [[nodiscard]]
    PawnTranspositionTable::PawnTTEntry BuildPawnEntry(const Board& b) noexcept {
        PawnTranspositionTable::PawnTTEntry entry;

        Score total = EvalPawnsAndKing(b, 0, entry.Passed[0]);
        total -= EvalPawnsAndKing(b, 1, entry.Passed[1]);

        entry.Mg = static_cast<int16_t>(total.mg);
        entry.Eg = static_cast<int16_t>(total.eg);

        return entry;
    }

    struct AttackMaps {
        // squares attacked by a piece type of a color
        uint64_t byPiece[2][6]{};
        // squares attacked by anything of a color
        uint64_t all[2]{};
        // squares attacked at least twice by a color
        uint64_t twice[2]{};
    };

    Score EvalPassers(
        const Board& b, int c, uint64_t passed, uint64_t occ, bool enemyToMove, const AttackMaps& am
    ) noexcept {
        if (!passed) return Score();

        const bool black = c == 1;
        const int e = c ^ 1;
        const uint64_t* pm = b.GetPieceMasks();

        const int ownKing = MaskToIndex(pm[c * 6 + 5]);
        const int oppKing = MaskToIndex(pm[e * 6 + 5]);

        // the enemy has only a king + pawns, so only his king can stop a passed pawn
        const bool onlyKingDefends = !(pm[e * 6 + 1] | pm[e * 6 + 2] | pm[e * 6 + 3] | pm[e * 6 + 4]);

        const uint64_t ownOcc = b.GetOccupancyMap(static_cast<PieceColor>(c));
        const uint64_t enemyOcc = b.GetOccupancyMap(static_cast<PieceColor>(e));
        const uint64_t rooksAndQueens = pm[3] | pm[4] | pm[9] | pm[10];

        Score s;

        for (uint64_t tmp = passed; tmp; tmp &= tmp - 1) {
            const int sq = MaskToIndex(tmp);
            const int f = ToFile(sq);
            const int r = ToRank(sq);
            const int adv = black ? r : (Ranks - 1 - r);

            const uint64_t front = LUTs::PassedPawn[c][sq] & Utils::FileMask(f);
            const int stop = sq + (black ? Files : -Files);

            // reduce bonus if a piece is blocking the promotion path, a lot more if it is the stop square
            int pct = 100;
            if (occ & IndexToMask(stop)) pct = PassedBlockedPct;
            else if (front & occ) pct = PassedBehindPct;

            s += Score(PassedBase[adv].mg * pct / 100, PassedBase[adv].eg * pct / 100);

            // king proximity to the stop square: the enemy king being far / our king being close matters
            // more the further the pawn has advanced
            if (adv >= 3) {
                const int w = 5 * adv - 13;
                const int oppD = ChebDist(oppKing, stop);
                const int ownD = ChebDist(ownKing, stop);

                s += Score(0, ((oppD * KingProxOpp / 4 - ownD * KingProxOwn) * w) / 4);

                {
                    // free to advance: nothing on the stop square and, the fewer enemy attackers / pieces
                    // there are on the way to the queening square, the better. Enemy rooks / queens behind the
                    // pawn x-ray the whole path
                    const uint64_t stopMask = IndexToMask(stop);

                    if (!(occ & stopMask)) {
                        const uint64_t behind = LUTs::PassedPawn[e][sq] & Utils::FileMask(f);

                        uint64_t unsafe = LUTs::PassedPawn[c][sq];
                        if (!(enemyOcc & behind & rooksAndQueens)) unsafe &= am.all[e] | enemyOcc;

                        int k = !unsafe ? PathFree : !(unsafe & front) ? PathClear : !(unsafe & stopMask) ? PathStop : 0;

                        // our own rook / queen behind the pawn, or the stop square is covered
                        if ((ownOcc & behind & rooksAndQueens) || (am.all[c] & stopMask)) k += PathDefended;

                        s += Score(k * w / PathDiv, k * w / PathDiv);
                    }
                }
            }

            // rule of the square: nothing but the enemy king can stop the pawn and it is out of reach
            if (onlyKingDefends && !(front & occ)) {
                int steps = Ranks - 1 - adv;
                if (adv == 1) --steps; // double push

                const int promoSq = black ? (Ranks * (Ranks - 1) + f) : f;
                const int kingDist = ChebDist(oppKing, promoSq) - (enemyToMove ? 1 : 0);

                if (kingDist > steps) s += Score(0, UnstoppableEg);
            }
        }

        return s;
    }

    Score EvalPieces(
        const Board& b, uint64_t occW, uint64_t occB, uint64_t occ, const uint64_t* passed, AttackMaps& am
    ) noexcept {
        const uint64_t* pm = b.GetPieceMasks();
        const uint64_t colorOcc[2] = {occW, occB};
        const uint64_t allPawns = pm[0] | pm[6];

        am.byPiece[0][0] = ((pm[0] & ~FileA) >> 9) | ((pm[0] & ~FileH) >> 7);
        am.byPiece[1][0] = ((pm[6] & ~FileA) << 7) | ((pm[6] & ~FileH) << 9);

        int kingSq[2];
        // running union of everything attacked so far by a color
        uint64_t running[2];

        for (int c = 0; c < 2; ++c) {
            kingSq[c] = MaskToIndex(pm[c * 6 + 5]);
            am.byPiece[c][5] = LUTs::KingAttacks[kingSq[c]];

            running[c] = am.byPiece[c][0] | am.byPiece[c][5];
            am.twice[c] = (am.byPiece[c][0] & am.byPiece[c][5]) | PawnDoubleAttacksBB(c, pm[c * 6]);
        }

        // every square an enemy pawn could ever attack
        const uint64_t pawnSpan[2] = {ForwardFill(0, am.byPiece[0][0]), ForwardFill(1, am.byPiece[1][0])};
        // pawns with something directly in front of them
        const uint64_t blockedPawns[2] = {pm[0] & (occ << 8), pm[6] & (occ >> 8)};

        // squares that count for mobility: not our own pieces, not controlled by an enemy pawn
        uint64_t mobilityArea[2];

        // king attack bookkeeping, indexed by the side whose king is being attacked
        int attackers[2] = {};
        int units[2] = {};
        int zoneHits[2] = {};

        Score total;

        for (int c = 0; c < 2; ++c) {
            const int off = c * 6;
            const int e = c ^ 1;
            const uint64_t myOcc = colorOcc[c];
            const uint64_t enemyPawnAttacks = am.byPiece[e][0];

            const uint64_t area = ~myOcc & ~enemyPawnAttacks;
            mobilityArea[c] = area;

            const uint64_t zone = LUTs::KingZone[e][kingSq[e]];

            const uint64_t forkTargets = pm[e * 6 + 3] | pm[e * 6 + 4] | pm[e * 6 + 5];

            // squares in the enemy half, defended by one of our pawns, that no enemy pawn can ever attack
            const uint64_t outposts = OutpostRanks[c] & am.byPiece[c][0] & ~pawnSpan[e];

            Score s;

            const auto addAttacks = [&](int pt, uint64_t atk) {
                am.byPiece[c][pt] |= atk;
                am.twice[c] |= running[c] & atk;
                running[c] |= atk;
                };

            const auto noteZone = [&](uint64_t atk, int weight) {
                if (atk & zone) {
                    ++attackers[e];
                    units[e] += weight;
                    zoneHits[e] += NumBitsOn(atk & zone);
                }
                };

            // terms shared by bishops and knights
            const auto minorTerms = [&](int sq, uint64_t atk, bool bishop) {
                const uint64_t sqMask = IndexToMask(sq);

                if (outposts & sqMask) s += bishop ? BishopOutpost : KnightOutpost;
                else if (outposts & atk & ~myOcc) s += bishop ? BishopReach : KnightReach;

                {
                    // sitting right behind a pawn is safe
                    if (Forward(c, sqMask) & allPawns) s += MinorBehind;

                    // minor pieces should stay close to their own king
                    s += KingProtector * ChebDist(sq, kingSq[c]);
                }
                };

            // bishops
            for (uint64_t tmp = pm[off + 1]; tmp; tmp &= tmp - 1) {
                const int sq = MaskToIndex(tmp);
                const uint64_t atk = MagicBitboard::DiagonalAttacks(sq, occ);
                addAttacks(1, atk);

                s += MobilityScore(1, NumBitsOn(atk & area));

                noteZone(atk, UnitBishop);

                {
                    minorTerms(sq, atk, true);

                    // bad bishop: our pawns on its color, much worse when the central ones are blocked
                    const int sameColor = NumBitsOn(pm[off] & (IsLightSquare(sq) ? LightSquaresMask : ~LightSquaresMask));
                    s += BadBishop * (sameColor * (1 + NumBitsOn(blockedPawns[c] & CenterFilesMask)));

                    // long diagonal bishop that sees both central squares through the pawns
                    if (NumBitsOn(MagicBitboard::DiagonalAttacks(sq, allPawns) & CenterSquares) >= 2) s += LongDiagonal;
                }
            }

            if (NumBitsOn(pm[off + 1]) >= 2) s += BishopPair;

            // knights
            for (uint64_t tmp = pm[off + 2]; tmp; tmp &= tmp - 1) {
                const int sq = MaskToIndex(tmp);
                const uint64_t atk = LUTs::KnightAttacks[sq];
                addAttacks(2, atk);

                s += MobilityScore(0, NumBitsOn(atk & area));

                if (NumBitsOn(atk & forkTargets) >= 2 && !(enemyPawnAttacks & IndexToMask(sq))) s += KnightFork;

                minorTerms(sq, atk, false);

                noteZone(atk, UnitKnight);
            }

            // rooks
            for (uint64_t tmp = pm[off + 3]; tmp; tmp &= tmp - 1) {
                const int sq = MaskToIndex(tmp);
                const uint64_t atk = MagicBitboard::OrthogonalAttacks(sq, occ);
                addAttacks(3, atk);

                const int n = NumBitsOn(atk & area);
                s += MobilityScore(2, n);

                // open / semi open file
                const uint64_t file = Utils::FileMask(ToFile(sq));
                const bool noMine = !(file & pm[off]);
                const bool noOpp = !(file & pm[e * 6]);
                if (noMine && noOpp) s += RookOpen;
                else if (noMine) s += RookSemi;

                {
                    // locked in by its own king: worse when we can not castle any more
                    if (!noMine && n <= 3) {
                        const int kf = ToFile(kingSq[c]);
                        if ((kf < 4) == (ToFile(sq) < kf)) {
                            const bool canCastle = ((b.GetCastlingRights() >> (2 * c)) & 3) != 0;
                            s += TrappedRook * (canCastle ? 1 : 2);
                        }
                    }

                    // a rook on the 5th rank or beyond that eyes enemy pawns
                    if (Advance(c, sq) >= 4) {
                        s += RookOnPawn * NumBitsOn(pm[e * 6] & MagicBitboard::OrthogonalAttacks(sq, 0ull));
                    }

                    // behind our own passed pawn
                    if (passed[c] & LUTs::PassedPawn[c][sq] & file) s += RookBehindPasser;
                }

                noteZone(atk, UnitRook);
            }

            // queens
            for (uint64_t tmp = pm[off + 4]; tmp; tmp &= tmp - 1) {
                const int sq = MaskToIndex(tmp);
                const uint64_t atk = MagicBitboard::CombinedAttacks(sq, occ);
                addAttacks(4, atk);

                s += MobilityScore(3, NumBitsOn(atk & area));


                noteZone(atk, UnitQueen);
            }

            if (c == 0) total += s;
            else total -= s;
        }

        for (int c = 0; c < 2; ++c) {
            am.all[c] = am.byPiece[c][0] | am.byPiece[c][1] | am.byPiece[c][2] |
                am.byPiece[c][3] | am.byPiece[c][4] | am.byPiece[c][5];
        }


        // ---- king danger, threats and space, from the point of view of the attacking side c ----
        for (int c = 0; c < 2; ++c) {
            const int e = c ^ 1;
            const int off = c * 6;
            const int eoff = e * 6;

            Score s; // positive = good for c

            {
                // safe checks: squares from which one of our pieces could check the king that the
                // enemy does not control
                const uint64_t safe = ~colorOcc[c] & ~am.all[e];

                const uint64_t knightChecks = LUTs::KnightAttacks[kingSq[e]];
                const uint64_t bishopChecks = MagicBitboard::DiagonalAttacks(kingSq[e], occ);
                const uint64_t rookChecks = MagicBitboard::OrthogonalAttacks(kingSq[e], occ);

                if (am.byPiece[c][2] & knightChecks & safe) units[e] += CheckKnight;
                if (am.byPiece[c][1] & bishopChecks & safe) units[e] += CheckBishop;
                if (am.byPiece[c][3] & rookChecks & safe) units[e] += CheckRook;
                if (am.byPiece[c][4] & (bishopChecks | rookChecks) & safe) units[e] += CheckQueen;

                {
                    // squares next to the king that we hit and the defender barely covers (only by his king or
                    // queen, and not twice)
                    const uint64_t zone = LUTs::KingZone[e][kingSq[e]];
                    const uint64_t weakRing =
                        zone & am.all[c] & ~am.twice[e] & (~am.all[e] | am.byPiece[e][5] | am.byPiece[e][4]);

                    units[e] += WeakRingUnit * NumBitsOn(weakRing);

                    // a knight / bishop that covers the squares around the king takes the sting out of attacks
                    if (am.byPiece[e][2] & am.byPiece[e][5]) units[e] += KnightDefUnit;
                    if (am.byPiece[e][1] & am.byPiece[e][5]) units[e] += BishopDefUnit;

                    // attacks on the flank the king lives on, in the defender's half of the board
                    const uint64_t flank = am.all[c] & KingFlank[ToFile(kingSq[e])] & KingCamp[e];
                    const int flankAttacks = NumBitsOn(flank) + NumBitsOn(flank & am.twice[c]);
                    s += FlankAttack * flankAttacks;
                }
            }

            int danger = units[e] * AttackerScale[std::min(attackers[e], 8)] / 100 + zoneHits[e] * ZoneHitUnit;

            // without a queen an attack on the king is much less dangerous
            if (!pm[off + 4]) danger = danger * QueenlessPct / 100;

            danger = std::clamp(danger, 0, DangerCap);

            total += (c == 0) ? Score(danger, 0) : Score(-danger, 0);

            {
                const uint64_t eMinors = pm[eoff + 1] | pm[eoff + 2];
                const uint64_t eRooks = pm[eoff + 3];
                const uint64_t eQueens = pm[eoff + 4];

                const uint64_t pawnAtk = am.byPiece[c][0];
                const uint64_t minorAtk = am.byPiece[c][1] | am.byPiece[c][2];
                const uint64_t rookAtk = am.byPiece[c][3];

                // pawns attacking pieces
                s += PawnAttackMinor * NumBitsOn(pawnAtk & eMinors);
                s += PawnAttackRook * NumBitsOn(pawnAtk & eRooks);
                s += PawnAttackQueen * NumBitsOn(pawnAtk & eQueens);

                // minors attacking rooks / queens, rooks attacking queens
                s += MinorAttackRook * NumBitsOn(minorAtk & eRooks);
                s += MinorAttackQueen * NumBitsOn(minorAtk & eQueens);
                s += RookAttackQueen * NumBitsOn(rookAtk & eQueens);

                const uint64_t ePieces = eMinors | eRooks | eQueens;

                {
                    // pieces (and pawns) of the enemy that are attacked by us and are not "strongly protected":
                    // protected by a pawn, or defended twice while we do not attack twice
                    const uint64_t eNoKing = colorOcc[e] & ~pm[eoff + 5];
                    const uint64_t stronglyProtected = am.byPiece[e][0] | (am.twice[e] & ~am.twice[c]);
                    const uint64_t weak = eNoKing & ~stronglyProtected & am.all[c];

                    // minors hitting weak pieces (the existing terms above already cover rooks / queens)
                    uint64_t t = weak & minorAtk;
                    s += WeakPawnByMinor * NumBitsOn(t & pm[eoff]);
                    s += WeakKnightByMinor * NumBitsOn(t & pm[eoff + 2]);
                    s += WeakBishopByMinor * NumBitsOn(t & pm[eoff + 1]);

                    // rooks hitting weak pieces
                    t = weak & rookAtk;
                    s += WeakPawnByRook * NumBitsOn(t & pm[eoff]);
                    s += WeakMinorByRook * NumBitsOn(t & eMinors);
                    s += WeakRookByRook * NumBitsOn(t & eRooks);

                    // our king can take a weak piece
                    if (weak & am.byPiece[c][5]) s += KingThreat;

                    // hanging: weak and not defended at all, or attacked twice with a non pawn target
                    const uint64_t hanging = weak & (~am.all[e] | (ePieces & am.twice[c]));
                    s += HangingPiece * NumBitsOn(hanging & ePieces);
                    s += HangingPawn * NumBitsOn(hanging & pm[eoff]);

                    // restricting enemy pieces
                    s += Restricted * NumBitsOn(am.all[e] & ~stronglyProtected & am.all[c]);

                    // safe pawn pushes that would attack something valuable
                    {
                        uint64_t push = Forward(c, pm[off]) & ~occ;
                        push |= Forward(c, push & (c ? 0x0000000000FF0000ull : 0x0000FF0000000000ull)) & ~occ;
                        push &= ~am.byPiece[e][0] & (~am.all[e] | am.all[c]);

                        s += PawnPush * NumBitsOn(PawnAttacksBB(c, push) & ePieces);
                    }

                    // knights / sliders that could hit the queen next move
                    if (NumBitsOn(eQueens) == 1) {
                        const int qs = MaskToIndex(eQueens);
                        const uint64_t safeSq = mobilityArea[c] & ~stronglyProtected;

                        s += KnightOnQueen * NumBitsOn(am.byPiece[c][2] & LUTs::KnightAttacks[qs] & safeSq);

                        const uint64_t sliders =
                            (am.byPiece[c][1] & MagicBitboard::DiagonalAttacks(qs, occ)) |
                            (am.byPiece[c][3] & MagicBitboard::OrthogonalAttacks(qs, occ));

                        s += SliderOnQueen * NumBitsOn(sliders & safeSq & am.twice[c]);
                    }
                }
            }


            if (c == 0) total += s;
            else total -= s;
        }

        return total;
    }

}

#pragma region Evaluation

static PawnTranspositionTable::PawnTTEntry GetPawnData(const Board& board) noexcept {
    PawnTranspositionTable& pawnTT = SearchContext::Data().PawnTT;

    if (const auto* cached = pawnTT.Probe(board.GetPawnAndKingHash())) {
        return *cached;
    }

    const PawnTranspositionTable::PawnTTEntry entry = BuildPawnEntry(board);
    pawnTT.Store(board.GetPawnAndKingHash(), entry);

    return entry;
}

int Board::evaluate(PieceColor sideToMove, float endgameWeight) const noexcept {
    const uint64_t occW = GetOccupancyMap(PieceColor::White);
    const uint64_t occB = GetOccupancyMap(PieceColor::Black);
    const uint64_t occ = occW | occB;

    const PawnTranspositionTable::PawnTTEntry pawnData = GetPawnData(*this);

    const bool whiteToMove = sideToMove == PieceColor::White;

    Score total(pawnData.Mg + m_IncMg, pawnData.Eg + m_IncEg);

    AttackMaps am;
    total += EvalPieces(*this, occW, occB, occ, pawnData.Passed, am);
    total += EvalPassers(*this, 0, pawnData.Passed[0], occ, !whiteToMove, am);
    total -= EvalPassers(*this, 1, pawnData.Passed[1], occ, whiteToMove, am);

    {
        // knights get better the more pawns there are on the board, rooks get worse: they want open lines.
        // Baseline is 5 own pawns
        for (int c = 0; c < 2; ++c) {
            const int delta = NumBitsOn(m_PieceMask[c * 6]) - 5;
            const Score adj =
                KnightPerPawn * (delta * NumBitsOn(m_PieceMask[c * 6 + 2])) +
                RookPerPawn * (delta * NumBitsOn(m_PieceMask[c * 6 + 3]));

            if (c == 0) total += adj;
            else total -= adj;
        }
    }


    // endgame scaling: some material advantages do not win. Only the endgame part of the score is scaled
    int scale = 64;

    if (total.eg != 0) {
        const int strong = total.eg > 0 ? 0 : 1;
        const int weak = strong ^ 1;

        const int strongPawns = NumBitsOn(m_PieceMask[strong * 6]);
        const int weakPawns = NumBitsOn(m_PieceMask[weak * 6]);
        const int npmDiff = NonPawnMaterial(m_PieceMask, strong) - NonPawnMaterial(m_PieceMask, weak);

        if (!strongPawns && npmDiff < 400) {
            // no pawns to promote and less than a rook up: can't force mate
            scale = weakPawns ? ScaleNoPawnsWeakPawns : ScaleNoPawns;
        } else if (!((m_PieceMask[2] | m_PieceMask[8]) | (m_PieceMask[4] | m_PieceMask[10]))) {
            const uint64_t sb = m_PieceMask[strong * 6 + 1];
            const uint64_t wb = m_PieceMask[weak * 6 + 1];

            const int strongRooks = NumBitsOn(m_PieceMask[strong * 6 + 3]);
            const int weakRooks = NumBitsOn(m_PieceMask[weak * 6 + 3]);

            // opposite colored bishops: drawish
            if (
                sb && wb && !(sb & (sb - 1)) && !(wb & (wb - 1)) &&
                strongRooks == weakRooks && strongRooks <= 1 &&
                IsLightSquare(MaskToIndex(sb)) != IsLightSquare(MaskToIndex(wb))
            ) {
                if (strongRooks == 0) scale = (strongPawns - weakPawns >= 3) ? ScaleOcbLead : ScaleOcb;
                else scale = ScaleOcbRooks;
            }
        }

        {
            // fewer pawns means a smaller chance to convert, as long as the material lead is less than a rook
            if (npmDiff < 400) scale = std::min(scale, ScaleBase + ScalePerPawn * strongPawns);

            // wrong colored bishop with only rook pawns against a bare king in the corner is a draw
            const uint64_t strongPawnMask = m_PieceMask[strong * 6];
            const uint64_t sb = m_PieceMask[strong * 6 + 1];

            if (
                strongPawnMask && sb && !(sb & (sb - 1)) &&
                !(m_PieceMask[strong * 6 + 2] | m_PieceMask[strong * 6 + 3] | m_PieceMask[strong * 6 + 4]) &&
                !weakPawns && !NonPawnMaterial(m_PieceMask, weak) &&
                (!(strongPawnMask & ~FileA) || !(strongPawnMask & ~FileH))
            ) {
                const int cornerFile = (strongPawnMask & FileA) ? 0 : (Files - 1);
                const int queeningSq = (strong == 0 ? 0 : Ranks * (Ranks - 1)) + cornerFile;
                const int weakKing = MaskToIndex(m_PieceMask[weak * 6 + 5]);

                if (IsLightSquare(MaskToIndex(sb)) != IsLightSquare(queeningSq) && ChebDist(weakKing, queeningSq) <= 1) {
                    scale = 0;
                }
            }
        }
    }

    // interpolate between middlegame and (scaled) endgame once
    const int egScaled = total.eg * scale / 64;
    int v = static_cast<int>(static_cast<float>(total.mg) + static_cast<float>(egScaled - total.mg) * endgameWeight);

    // when one side is clearly winning on material and the other has nothing to push, drive the
    // lone king to the edge / corner and bring our own king closer. Only for the winning side.
    for (int win = 0; win < 2; ++win) {
        const int lose = win ^ 1;

        if (
            !m_PieceMask[lose * 6] &&
            (m_PieceMask[win * 6 + 1] | m_PieceMask[win * 6 + 2] | m_PieceMask[win * 6 + 3] | m_PieceMask[win * 6 + 4]) &&
            NonPawnMaterial(m_PieceMask, win) - NonPawnMaterial(m_PieceMask, lose) >= 400
        ) {
            const int loserKing = MaskToIndex(m_PieceMask[lose * 6 + 5]);
            const int winnerKing = MaskToIndex(m_PieceMask[win * 6 + 5]);

            const int rank = ToRank(loserKing);
            const int file = ToFile(loserKing);

            int cornerTerm = (std::max(3 - rank, rank - 4) + std::max(3 - file, file - 4)) * 10;

            // bishop + knight can only mate in a corner that matches the bishop's color
            if (
                NumBitsOn(m_PieceMask[win * 6 + 1]) == 1 && NumBitsOn(m_PieceMask[win * 6 + 2]) == 1 &&
                !(m_PieceMask[win * 6 + 3] | m_PieceMask[win * 6 + 4])
            ) {
                const bool light = IsLightSquare(MaskToIndex(m_PieceMask[win * 6 + 1]));
                // a8 (0) and h1 (63) are light, h8 (7) and a1 (56) are dark
                const int c1 = light ? 0 : 7;
                const int c2 = light ? 63 : 56;
                const int dist = std::min(ManhattanDist(loserKing, c1), ManhattanDist(loserKing, c2));

                cornerTerm = (14 - dist) * 10;
            }

            const int drive = cornerTerm + (14 - ManhattanDist(loserKing, winnerKing)) * 5;
            const int scaled = static_cast<int>(static_cast<float>(drive) * endgameWeight);

            v += (win == 0) ? scaled : -scaled;
        }
    }

    return Tempo + (whiteToMove ? v : -v); // tempo bonus for being the side to move
}

int Board::evaluate(PieceColor sideToMove) const noexcept {
    return evaluate(sideToMove, getEndgameWeight());
}

#pragma region SEE

uint64_t Board::seeGetAllAttackers(
    int pos, uint64_t knights, uint64_t diagSliders,
    uint64_t orthSliders, uint64_t kings, uint64_t occupancy
) const {
    return occupancy & (
        // white pawns that attack `pos` stand where a *black* pawn on `pos` would attack
        (LUTs::PawnAttacks[1][pos] & m_PieceMask[0]) |
        // black pawns that attack `pos` stand where a *white* pawn on `pos` would attack
        (LUTs::PawnAttacks[0][pos] & m_PieceMask[6]) |
        // knights
        (LUTs::KnightAttacks[pos] & knights) |
        // bishops / queens
        (MagicBitboard::DiagonalAttacks(pos, occupancy) & diagSliders) |
        // rooks / queens
        (MagicBitboard::OrthogonalAttacks(pos, occupancy) & orthSliders) |
        // king
        (LUTs::KingAttacks[pos] & kings)
    );
}

int Board::see(Move move) const {
    if (!HasFlag(move.Flag, MoveFlag::Capture)) return 0;

    const bool isEnPassant = HasFlag(move.Flag, MoveFlag::EnPassant);

    const int toSq = move.TargetSquare;
    const int fromSq = move.StartingSquare;

    const PieceType capturedType = isEnPassant ? PieceType::Pawn : m_Mailbox[toSq].Type;

    int gain[32];
    int depth = 0;
    gain[0] = PieceValues[static_cast<int>(capturedType)];

    // full occypancy - attacker and en-passant victim
    uint64_t occ = GetOccupancyMap() & ~IndexToMask(fromSq);

    if (isEnPassant) {
        const int dir = m_Mailbox[fromSq].Color == PieceColor::Black ? Files : -Files;
        occ &= ~IndexToMask(toSq - dir);
    }

    const uint64_t bishopsMask = m_PieceMask[1] | m_PieceMask[7];
    const uint64_t knightsMask = m_PieceMask[2] | m_PieceMask[8];
    const uint64_t rooksMask = m_PieceMask[3] | m_PieceMask[9];
    const uint64_t queensMask = m_PieceMask[4] | m_PieceMask[10];
    const uint64_t kingsMask = m_PieceMask[5] | m_PieceMask[11];

    const uint64_t diagSliders = bishopsMask | queensMask;
    const uint64_t orthSliders = rooksMask | queensMask;

    static constexpr PieceType Order[] = {
        PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
        PieceType::Rook, PieceType::Queen, PieceType::King
    };

    uint64_t attackers = seeGetAllAttackers(toSq, knightsMask, diagSliders, orthSliders, kingsMask, occ);
    PieceType lastMoved = m_Mailbox[fromSq].Type;
    PieceColor side = InvertColor(m_Mailbox[fromSq].Color);

    while (true) {
        const int colorOff = side == PieceColor::Black ? 6 : 0;

        // find least valuable attacker for `side`
        uint64_t attacker = 0ull;
        PieceType attackerType = PieceType::None;

        for (const PieceType pt : Order) {
            if (uint64_t candidates = attackers & m_PieceMask[static_cast<int>(pt) + colorOff]) {
                attacker = candidates & ~(candidates - 1);
                attackerType = pt;

                break;
            }
        }

        if (!attacker) break;

        gain[depth + 1] = (lastMoved == PieceType::King ? 20000 : PieceValues[static_cast<int>(lastMoved)]) - gain[depth];
        ++depth;

        // remove the attacker from occupancy
        occ &= ~attacker;
        lastMoved = attackerType;
        attackers = seeGetAllAttackers(toSq, knightsMask, diagSliders, orthSliders, kingsMask, occ);

        side = InvertColor(side);
    }

    // rollback
    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        --depth;
    }

    return gain[0];
}

#pragma region Confidence

float Board::GetConfidence(PieceColor sideToMove, int thinkTimeMS) const {
    SearchContext::BeginNew(m_Hash);
    SearchContext::RootSide = sideToMove;

    SearchContext::Data().PositionHistory[1] = m_Hash;

    // cancel search after time limit expires
    std::thread timerThread([thinkTimeMS]() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(thinkTimeMS);

        while (std::chrono::steady_clock::now() < deadline) {
            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        SearchContext::Cancelled.store(true, std::memory_order_relaxed);
    });

    int deepestScore = 0;
    int depth = 0;

    do {
        const int score = negamax(++depth, -INF, INF, sideToMove);

        if (score != SearchCancelScore && score != -SearchCancelScore) {
            deepestScore = score;
        }
    } while (!SearchContext::Cancelled.load(std::memory_order_relaxed));

    timerThread.join();

    return CentripawnToUniform(sideToMove == PieceColor::White ? deepestScore : -deepestScore);
}

#pragma region Move Ranking

int Board::scoreMove(Move move, PieceColor sideToMove) const noexcept {
    const bool isCapture = HasFlag(move.Flag, MoveFlag::Capture);

    // history is only meaningful for quiet moves, keep it out of capture / promotion ordering
    int score = (isCapture || isPromotion(move.Flag))
        ? 0 : SearchContext::Data().HistoryTable[SearchContext::HistoryIndex(sideToMove, move.StartingSquare, move.TargetSquare)];

    // 1. promotions (highest priority)
    if (isPromotion(move.Flag)) {
        score += PieceValues[static_cast<int>(Utils::PromotionPiece(move.Flag))] * 40;
    }

    // 2. captures (most valuable victim - least valuable attacker)
    if (isCapture) {
        const PieceType victimType = HasFlag(move.Flag, MoveFlag::EnPassant)
            ? PieceType::Pawn : m_Mailbox[move.TargetSquare].Type;

        const int victimValue = PieceValues[static_cast<int>(victimType)];
        const int attackerValue = PieceValues[static_cast<int>(m_Mailbox[move.StartingSquare].Type)];
        const int gain = victimValue - attackerValue;

        if (gain > 0) {
            // winning capture
            score += 20'000 + gain + victimValue;
        } else if (gain == 0) {
            // equal capture
            score += 14'000 + victimValue;
        } else {
            // losing capture
            score += 6'000 + gain;
        }
    }

    // 3. checks
    if (HasFlag(move.Flag, MoveFlag::Check)) {
        score += 50;
    }

    // 4. castling
    if (isCastle(move.Flag)) {
        score += 50;
    }

    return score;
}

#pragma region Quiescence

static inline void PickBestMove(ScoredMove* moves, unsigned int count, unsigned int start) noexcept {
    unsigned int best = start;

    for (unsigned int j = start + 1u; j < count; ++j) {
        if (moves[j].Score > moves[best].Score) best = j;
    }

    if (best != start) std::swap(moves[start], moves[best]);
}

int Board::quiescence(int alpha, int beta, PieceColor sideToMove, int ply) const {
    SearchContext::CountNode(ply);

    const uint64_t checkMask = getCheckMask(sideToMove, GetOccupancyMap());

    // when in check standing pat is not allowed (we can't just "pass"), every evasion has to be searched
    const bool inCheck = checkMask != ~0ull;

    int standPat = -INF;

    if (!inCheck) {
        standPat = evaluate(sideToMove);

        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;
    }

    UninitArray<Move, MaxLegalMoves> captureMoves;
    unsigned int numCaptures = 0u;

    if (inCheck) getAllLegalMoves(sideToMove, checkMask, captureMoves, numCaptures);
    else getAllCaptureMoves(sideToMove, checkMask, captureMoves, numCaptures);

    if (inCheck && numCaptures == 0u) return ply - INF; // checkmate

    UninitArray<ScoredMove, MaxLegalMoves> moves;
    unsigned int numMoves = 0u;

    for (unsigned int i = 0u; i < numCaptures; ++i) {
        const Move move = captureMoves[i];

        if (!inCheck) {
            const PieceType victimType = HasFlag(move.Flag, MoveFlag::EnPassant)
                ? PieceType::Pawn : m_Mailbox[move.TargetSquare].Type;

            int gain = PieceValues[static_cast<int>(victimType)];

            if (isPromotion(move.Flag)) {
                gain += PieceValues[static_cast<int>(Utils::PromotionPiece(move.Flag))] - PieceValues[0];
            }

            // delta pruning: skip if even a perfect capture can't raise alpha
            if (standPat + gain + 200 < alpha) continue;

            // captures that lose material by static exchange are almost never worth searching
            if (!isPromotion(move.Flag) && see(move) < 0) continue;
        }

        moves[numMoves++] = ScoredMove(move, scoreMove(move, sideToMove));
    }

    if (numMoves == 0u) return alpha;

    for (unsigned int i = 0u; i < numMoves; ++i) {
        PickBestMove(moves, numMoves, i);

        Board copy = *this;
        copy.doMoveInternal(moves[i].Move);

        const int score = -copy.quiescence(-beta, -alpha, InvertColor(sideToMove), ply + 1);

        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }

    return alpha;
}

#pragma region Negamax

static inline int getSearchExtension(Move move) noexcept {
    return HasFlag(move.Flag, MoveFlag::Check);
}

static inline int depthReduction(int ply, int moveNumber) noexcept {
    return LUTs::Reduction[std::min(ply, SearchContext::MaxPly - 1)][std::min(moveNumber, SearchContext::MaxMoves - 1)];
}

[[nodiscard]]
static inline int ContinuationKey(Piece piece, int to) noexcept {
    return (static_cast<int>(piece.Type) + (piece.Color == PieceColor::Black ? 6 : 0)) * (Ranks * Files) + to;
}

int Board::negamax(int depth, int alpha, int beta, PieceColor sideToMove, int ply, int ext) const {
    if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

    SearchContext::CountNode(ply);

    SearchThreadData& td = SearchContext::Data();

    const bool pvNode = beta - alpha > 1;
    const bool excluded = static_cast<bool>(td.ExcludedMove[ply]);

    // draw by game design
    {
        const bool fifty = FiftyMoveRule();

        if (fifty || isThreefoldRepetition(ply) || HasInsufficientMaterial()) {
            // checkmate on the move that reaches the fifty move limit is still checkmate
            const bool mated = fifty && isUnderCheck(sideToMove) && !HasAnyLegalMoves(sideToMove);

            if (!mated) {
                // penalize draws more when we have an advantage
                const int stmEval = evaluate(sideToMove);
                const bool engineToMove = sideToMove == SearchContext::RootSide;
                const int engineEval = engineToMove ? stmEval : -stmEval;
                const int drawForEngine = -std::clamp(Options::Contempt + engineEval / 20, -200, 200);

                return engineToMove ? drawForEngine : -drawForEngine;
            }
        }
    }

    if (depth <= 0) {
        return quiescence(alpha, beta, sideToMove, ply + 1);
    }

    if (ply >= SearchContext::MaxPly - 2) {
        return evaluate(sideToMove);
    }

    // single transposition table lookup, used for the cutoff, move ordering and the singular extension
    TranspositionTable::TTEntry tte;
    const bool ttHit = GameContext::TT.Peek(m_Hash, tte);

    Move ttMove;
    if (ttHit) {
        ttMove = Move(tte.BestMove.StartingSquare, tte.BestMove.TargetSquare);

        const bool ttCutoffAllowed = !excluded && !(ply <= 6 && m_HalfMoveClock >= 2);

        if (ttCutoffAllowed && tte.Depth >= depth) {
            const bool usable =
                tte.NodeFlag == TranspositionTable::TTFlag::Exact ||
                (tte.NodeFlag == TranspositionTable::TTFlag::LowerBound && tte.Score >= beta) ||
                (tte.NodeFlag == TranspositionTable::TTFlag::UpperBound && tte.Score <= alpha);

            if (usable) {
                if (tte.Score > MateThreshold)  return tte.Score - ply;
                if (tte.Score < -MateThreshold) return tte.Score + ply;
                return tte.Score;
            }
        }
    }

    // (~0 = not in check, 0 = double check, otherwise the squares that capture / block the single checker)
    const uint64_t checkMask = getCheckMask(sideToMove, GetOccupancyMap());
    const bool inCheck = checkMask != ~0ull;
    const float endgameWeight = getEndgameWeight();

    const int staticEval = inCheck ? SearchLimits::NoEval : evaluate(sideToMove, endgameWeight);
    td.StaticEval[ply] = staticEval;

    // is the position getting better for us compared to our last turn(s)? if not, prune / reduce more
    bool improving = false;
    if (!inCheck) {
        if (ply >= 2 && td.StaticEval[ply - 2] != SearchLimits::NoEval) {
            improving = staticEval > td.StaticEval[ply - 2];
        } else if (ply >= 4 && td.StaticEval[ply - 4] != SearchLimits::NoEval) {
            improving = staticEval > td.StaticEval[ply - 4];
        }
    }

    // moves that lead into this node, for continuation history
    const int contKey1 = td.ContKey[ply];
    const int contKey2 = ply >= 1 ? td.ContKey[ply - 1] : -1;

    // reverse futility pruning
    if (
        !pvNode && !inCheck && !excluded &&
        depth <= 6 &&
        std::abs(beta) < MateThreshold
    ) {
        if (staticEval - (120 * depth - (improving ? 60 : 0)) >= beta) {
            return staticEval;
        }
    }

    // null move pruning
    if (
        !pvNode && !inCheck && !excluded &&
        !td.SkipNull[ply] &&
        depth >= 3 &&
        staticEval >= beta &&
        endgameWeight < 0.6f &&
        // zugzwang is only a real danger when the side to move has nothing but pawns and a king
        (m_PieceMask[(sideToMove == PieceColor::Black ? 6 : 0) + 1] | m_PieceMask[(sideToMove == PieceColor::Black ? 6 : 0) + 2] |
            m_PieceMask[(sideToMove == PieceColor::Black ? 6 : 0) + 3] | m_PieceMask[(sideToMove == PieceColor::Black ? 6 : 0) + 4])
    ) {
        Board copy = *this;

        if (copy.m_EnPassantSquare != NullPos) {
            copy.m_Hash = ZobristHasher::HashEnPassantSquare(copy.m_Hash, copy.m_EnPassantSquare);
        }

        copy.m_EnPassantSquare = NullPos;
        copy.m_Hash = ZobristHasher::HashSideToMove(copy.m_Hash);

        td.PositionHistory[ply + 1] = copy.m_Hash;
        td.ContKey[ply + 1] = -1;
        td.SkipNull[ply + 1] = true; // no two null moves in a row

        const int reduction = 3 + depth / 4 + std::min((staticEval - beta) / 200, 2);
        const int score = -copy.negamax(
            depth - 1 - reduction, -beta, -beta + 1, InvertColor(sideToMove), ply + 1, ext
        );

        td.SkipNull[ply + 1] = false;

        if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

        // never trust a mate score that came out of a null move
        if (score >= beta) return score >= MateThreshold ? beta : score;
    }

    // probcut
    if (
        !pvNode && !inCheck && !excluded &&
        depth >= 5 &&
        std::abs(beta) < MateThreshold
    ) {
        const int probcutBeta = beta + 200;

        // only captures are ever tried here, so only generate captures (instead of all legal moves)
        UninitArray<Move, MaxLegalMoves> captureMoves;
        unsigned int numCaptures = 0u;
        getAllCaptureMoves(sideToMove, checkMask, captureMoves, numCaptures);

        for (unsigned int i = 0; i < numCaptures; ++i) {
            const Move move = captureMoves[i];

            if (see(move) < probcutBeta - staticEval) {
                continue;
            }

            Board copy = *this;
            copy.doMoveInternal(move);

            td.PositionHistory[ply + 1] = copy.m_Hash;
            td.ContKey[ply + 1] = ContinuationKey(m_Mailbox[move.StartingSquare], move.TargetSquare);

            const int score = -copy.negamax(
                depth - 4, -probcutBeta, -probcutBeta + 1, InvertColor(sideToMove), ply + 1, ext
            );

            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

            if (score >= probcutBeta) return score;
        }
    }

    // internal iterative reduction: without a TT move this node is poorly ordered, don't spend full depth on it
    if (!inCheck && depth >= 4 && !ttMove) {
        --depth;
    }

    // singular extension: if the TT move is much better than everything else it deserves more depth
    int singularExtension = 0;

    if (
        !excluded && !inCheck &&
        depth >= 6 &&
        ext < SearchContext::MaxExt &&
        ttMove && ttHit &&
        tte.Depth >= depth - 3 &&
        tte.NodeFlag != TranspositionTable::TTFlag::UpperBound &&
        std::abs(tte.Score) < MateThreshold
    ) {
        const int singularBeta = tte.Score - 2 * depth;

        td.ExcludedMove[ply] = ttMove;

        const int singularScore = negamax(
            (depth - 1) / 2, singularBeta - 1, singularBeta, sideToMove, ply, ext + 1
        );

        td.ExcludedMove[ply] = Move();

        if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

        if (singularScore < singularBeta) {
            singularExtension = 1;

            // far below the bar, in a non PV node: extend twice
            if (!pvNode && singularScore < singularBeta - 40) singularExtension = 2;
        } else if (singularBeta >= beta) {
            // multi-cut: even without the TT move this node fails high
            return singularBeta;
        } else if (tte.Score >= beta) {
            // the TT move is probably good enough on its own, but nothing singular about it: search less
            singularExtension = -1;
        }
    }

    UninitArray<ScoredMove, MaxLegalMoves> moves;
    unsigned int numMoves = 0u;

    {
        UninitArray<Move, MaxLegalMoves> legalMoves;
        unsigned int numLegal = 0u;

        getAllLegalMoves(sideToMove, checkMask, legalMoves, numLegal);

        for (unsigned int i = 0u; i < numLegal; ++i) {
            const Move move = legalMoves[i];

            if (move == td.ExcludedMove[ply]) continue;

            int moveScore = scoreMove(move, sideToMove);

            const bool isCapture = HasFlag(move.Flag, MoveFlag::Capture);

            // give massive boost to best move from TT
            if (move == ttMove) {
                moveScore += INF;
            } else if (isCapture) {
                // a capture that looks losing by piece values may still be fine (e.g. the victim is undefended)
                const int victimValue = HasFlag(move.Flag, MoveFlag::EnPassant)
                    ? PieceValues[0] : PieceValues[static_cast<int>(m_Mailbox[move.TargetSquare].Type)];

                if (victimValue < PieceValues[static_cast<int>(m_Mailbox[move.StartingSquare].Type)] && see(move) >= 0) {
                    moveScore += 8000;
                }
            } else if (!isPromotion(move.Flag)) {
                const int key = ContinuationKey(m_Mailbox[move.StartingSquare], move.TargetSquare);
                moveScore += SearchContext::ContHistoryScore(contKey1, key) + SearchContext::ContHistoryScore(contKey2, key);

                // give score to quiet moves if they are actually killer
                if (!HasFlag(move.Flag, MoveFlag::Check)) {
                    if (move == td.killerMoves[ply][0]) moveScore += 5000;
                    if (move == td.killerMoves[ply][1]) moveScore += 4000;
                }
            }

            moves[numMoves++] = ScoredMove(move, moveScore);
        }
    }

    if (numMoves == 0u) {
        if (excluded) {
            return alpha;
        }

        if (inCheck) {
            if (
                SearchContext::CatchAllMode &&
                sideToMove != SearchContext::EnginePlayer
            ) {
                // If the engine is delivering checkmate
                if (const int oppPiecesRemaining = NumBitsOn(GetOccupancyMap(sideToMove)) - 1) {
                    return 500 + (oppPiecesRemaining * 100);
                }
            }

            return ply - INF;
        }

        return 0; // stalemate
    }

    int bestScore = -INF;
    const int originalAlpha = alpha;
    Move bestMove;

    UninitArray<Move, MaxLegalMoves> quietsTried;
    unsigned int numQuietsTried = 0u;

    for (unsigned int i = 0u; i < numMoves; ++i) {
        // lazy selection sort: only the moves that are actually reached get ordered
        PickBestMove(moves, numMoves, i);

        const Move move = moves[i].Move;

        const bool isCapture = HasFlag(move.Flag, MoveFlag::Capture);
        const bool isCheckMove = HasFlag(move.Flag, MoveFlag::Check);
        const bool isPromo = isPromotion(move.Flag);
        const bool isQuiet = !isCapture && !isCheckMove && !isPromo;

        const Piece mover = m_Mailbox[move.StartingSquare];
        const int contKey = ContinuationKey(mover, move.TargetSquare);

        // combined history score of this move, used for pruning and reductions
        int histScore = 0;
        if (isQuiet) {
            histScore =
                td.HistoryTable[SearchContext::HistoryIndex(sideToMove, move.StartingSquare, move.TargetSquare)] +
                SearchContext::ContHistoryScore(contKey1, contKey) +
                SearchContext::ContHistoryScore(contKey2, contKey);
        }

        // pruning of moves that are very unlikely to matter. Never done for the first move, when in check,
        if (!inCheck && bestScore > -MateThreshold) {
            static constexpr int LMPTable[] = {0, 8, 12, 18, 24};

            // late move pruning
            if (isQuiet && depth <= 4 && i >= static_cast<unsigned>(LMPTable[depth] >> (improving ? 0 : 1))) {
                continue;
            }

            // futility pruning: the static eval is so far below alpha that a quiet move can't realistically recover
            if (isQuiet && depth <= 5 && staticEval + 100 + 90 * depth <= alpha) {
                continue;
            }

            // moves that lose a lot of material by exchange, at low depth
            if (!pvNode && isCapture && depth <= 5 && see(move) < -100 * depth) {
                continue;
            }
        }

        Board copy = *this;
        copy.doMoveInternal(move);

        // start loading the child's TT slot now, it is probed as soon as the child node starts
        GameContext::TT.Prefetch(copy.m_Hash);

        td.PositionHistory[ply + 1] = copy.m_Hash;
        td.ContKey[ply + 1] = contKey;

        int extension = 0;
        if (ext < SearchContext::MaxExt) extension += getSearchExtension(move);
        if (move == ttMove && !(singularExtension > 0 && ext >= SearchContext::MaxExt)) extension += singularExtension;
        extension = std::min(extension, 2);

        const int newExt = ext + std::max(extension, 0);
        const int nextDepth = depth - 1 + extension;

        int score;

        if (i == 0) {
            // first move: full window
            score = -copy.negamax(nextDepth, -beta, -alpha, InvertColor(sideToMove), ply + 1, newExt);
        } else {
            bool searchFull = true;

            // late move reduction
            if (depth >= 3 && i > 2 && !isCapture && !isCheckMove) {
                int reduction = depthReduction(depth, i + 1);

                if (pvNode) --reduction;
                if (!improving) ++reduction;
                if (move == td.killerMoves[ply][0] || move == td.killerMoves[ply][1]) --reduction;

                // good history -> reduce less, bad history -> reduce more
                reduction -= std::clamp(histScore / 5000, -2, 2);

                if (reduction > 0) {
                    const int reducedDepth = std::max(1, nextDepth - reduction);

                    score = -copy.negamax(reducedDepth, -alpha - 1, -alpha, InvertColor(sideToMove), ply + 1, newExt);

                    if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

                    // only look at it again at full depth if the reduced search surprised us
                    searchFull = score > alpha && reducedDepth < nextDepth;
                } else {
                    score = alpha + 1; // forces the scout below
                }
            } else {
                score = alpha + 1;
            }

            // scout with a null window at full depth
            if (searchFull) {
                score = -copy.negamax(nextDepth, -alpha - 1, -alpha, InvertColor(sideToMove), ply + 1, newExt);

                if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;
            }

            // re-search with the full window if the scout landed inside it (PV nodes only, otherwise beta = alpha + 1)
            if (score > alpha && score < beta) {
                score = -copy.negamax(nextDepth, -beta, -alpha, InvertColor(sideToMove), ply + 1, newExt);
            }
        }

        if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return SearchCancelScore;

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            // what seemed like a quiet move, is actually killer
            if (isQuiet) {
                const int bonus = std::min(depth * depth * 4, 1600);

                SearchContext::UpdateHistory(sideToMove, move.StartingSquare, move.TargetSquare, bonus);
                SearchContext::UpdateContHistory(contKey1, contKey, bonus);
                SearchContext::UpdateContHistory(contKey2, contKey, bonus);

                // quiet moves that were tried earlier and failed to cut get penalised
                for (unsigned int q = 0u; q < numQuietsTried; ++q) {
                    const Move failed = quietsTried[q];
                    const int failedKey = ContinuationKey(m_Mailbox[failed.StartingSquare], failed.TargetSquare);

                    SearchContext::UpdateHistory(sideToMove, failed.StartingSquare, failed.TargetSquare, -bonus);
                    SearchContext::UpdateContHistory(contKey1, failedKey, -bonus);
                    SearchContext::UpdateContHistory(contKey2, failedKey, -bonus);
                }

                // add to the shift register
                td.killerMoves[ply][1] = td.killerMoves[ply][0];
                td.killerMoves[ply][0] = move;
            }

            break;
        }

        if (isQuiet && numQuietsTried < MaxLegalMoves) {
            quietsTried[numQuietsTried++] = move;
        }
    }

    TranspositionTable::TTFlag flag;
    if (bestScore <= originalAlpha) flag = TranspositionTable::TTFlag::UpperBound;
    else if (bestScore >= beta) flag = TranspositionTable::TTFlag::LowerBound;
    else flag = TranspositionTable::TTFlag::Exact;

    int ttStoreScore = bestScore;
    if (bestScore > MateThreshold) ttStoreScore = bestScore + ply;
    if (bestScore < -MateThreshold) ttStoreScore = bestScore - ply;

    if (!excluded) {
        // a fail low node has no real best move, don't let it pollute move ordering
        GameContext::TT.Store(
            m_Hash, depth, ttStoreScore, flag,
            flag == TranspositionTable::TTFlag::UpperBound ? Move() : bestMove
        );
    }

    return bestScore;
}

int Board::evaluateMove(Move move, PieceColor sideToMove, int depth, int alpha, int beta) const {
    Board copy = *this;
    copy.doMoveInternal(move);

    SearchThreadData& td = SearchContext::Data();

    td.PositionHistory[1] = copy.m_Hash;
    td.ContKey[0] = -1;
    td.ContKey[1] = ContinuationKey(m_Mailbox[move.StartingSquare], move.TargetSquare);

    return -copy.negamax(depth - 1, -beta, -alpha, InvertColor(sideToMove));
}

bool Board::searchRoot(
    Move* moves, unsigned int numMoves, PieceColor sideToMove, int depth,
    Move& bestMove, int& bestScore, bool allowPartial
) const {
    const auto cancelled = []() { return SearchContext::Cancelled.load(std::memory_order_relaxed); };

    int rootScores[MaxLegalMoves];

    int delta = 25;
    int aspirationAlpha = (depth >= 4 && bestScore > -INF) ? bestScore - delta : -INF;
    int aspirationBeta = (depth >= 4 && bestScore < INF) ? bestScore + delta : INF;

    Move iterationBest = bestMove;
    int iterationScore = -INF;

    while (true) {
        int alpha = aspirationAlpha;
        const int beta = aspirationBeta;

        iterationScore = -INF;
        bool failedHigh = false;

        for (unsigned int i = 0u; i < numMoves; ++i) {
            rootScores[i] = -INF;
        }

        for (unsigned int i = 0u; i < numMoves; ++i) {
            const Move move = moves[i];

            int score;

            if (i == 0u) {
                score = evaluateMove(move, sideToMove, depth, alpha, beta);
            } else {
                // principal variation search: only the best move needs an exact score, so scout with a null window
                score = evaluateMove(move, sideToMove, depth, alpha, alpha + 1);

                if (!cancelled() && score > alpha && score < beta) {
                    score = evaluateMove(move, sideToMove, depth, alpha, beta);
                }
            }

            if (cancelled()) {
                if (allowPartial && score != SearchCancelScore && score != -SearchCancelScore && score > bestScore) {
                    bestMove = move;
                    bestScore = score;
                }

                return false;
            }

            rootScores[i] = score;

            if (score > iterationScore) {
                iterationScore = score;
                iterationBest = move;
            }

            if (score > alpha) alpha = score;

            // the true score is at least beta: stop here and search again with a wider window,
            // the remaining moves would only be searched with an empty window
            if (score >= beta) {
                failedHigh = true;
                break;
            }
        }

        if (failedHigh) {
            aspirationBeta = delta > 400 ? INF : std::min<int>(INF, aspirationBeta + delta);
            delta *= 2;
            continue;
        }

        if (iterationScore <= aspirationAlpha && aspirationAlpha > -INF) {
            aspirationAlpha = delta > 400 ? -INF : std::max<int>(-INF, aspirationAlpha - delta);
            delta *= 2;
            continue;
        }

        break; // the score is inside the window, accept it
    }

    bestMove = iterationBest;
    bestScore = iterationScore;

    // best move first, then by the scores of this iteration
    unsigned int order[MaxLegalMoves];
    for (unsigned int i = 0u; i < numMoves; ++i) order[i] = i;

    std::stable_sort(order, order + numMoves, [&](unsigned int a, unsigned int b) {
        const bool aBest = moves[a] == bestMove;
        const bool bBest = moves[b] == bestMove;
        if (aBest != bBest) return aBest;
        return rootScores[a] > rootScores[b];
    });

    Move sorted[MaxLegalMoves];
    for (unsigned int i = 0u; i < numMoves; ++i) sorted[i] = moves[order[i]];
    for (unsigned int i = 0u; i < numMoves; ++i) moves[i] = sorted[i];

    return true;
}

#pragma region Find By Time

void Board::searchHelper(PieceColor sideToMove, int helperId) const {
    // depth skipping pattern, same idea as used by other Lazy SMP engines
    static constexpr int SkipSize[20] = {1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4};
    static constexpr int SkipPhase[20] = {0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 7};

    auto data = std::make_unique<SearchContext::ThreadData>();
    SearchContext::Tls = data.get();

    data->PawnTT.SetSize(std::max(1u, SearchContext::MainData.PawnTT.GetSize()));
    data->PositionHistory[0] = m_Hash;

    ScoredMove allMoves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    {
        Move legalMoves[MaxLegalMoves];
        getAllLegalMoves(sideToMove, legalMoves, numMoves);

        for (unsigned int i = 0u; i < numMoves; ++i) {
            allMoves[i] = ScoredMove(legalMoves[i], scoreMove(legalMoves[i], sideToMove));
        }
    }

    if (numMoves > 1u) {
        std::sort(allMoves, allMoves + numMoves,
            [](const ScoredMove& a, const ScoredMove& b) {
                return a.Score > b.Score;
            }
        );

        Move rootMoves[MaxLegalMoves];
        for (unsigned int i = 0u; i < numMoves; ++i) rootMoves[i] = allMoves[i].Move;

        const int idx = (helperId - 1) % 20;

        Move bestMove = rootMoves[0];
        int bestScore = -INF;

        for (int depth = 1; depth <= 96 && !SearchContext::Cancelled.load(std::memory_order_relaxed); ++depth) {
            if (((depth + SkipPhase[idx]) / SkipSize[idx]) % 2) continue;

            if (!searchRoot(rootMoves, numMoves, sideToMove, depth, bestMove, bestScore, false)) break;
        }
    }

    SearchContext::Tls = &SearchContext::MainData;
}

namespace {
    std::vector<std::thread> StartHelperThreads(const std::function<void(int)>& work) {
        std::vector<std::thread> helpers;

        const int count = std::clamp(Options::Threads, 1, 128) - 1;
        helpers.reserve(count);

        for (int i = 1; i <= count; ++i) {
            helpers.emplace_back([work, i]() { work(i); });
        }

        return helpers;
    }

    void StopHelperThreads(std::vector<std::thread>& helpers) {
        SearchContext::Cancelled.store(true, std::memory_order_relaxed);

        for (std::thread& t : helpers) {
            if (t.joinable()) t.join();
        }

        helpers.clear();
    }

    void PrintUciInfo(
        int depth, int score, const Move& bestMove,
        const std::chrono::steady_clock::time_point& searchStart
    ) {
        const int selDepth = SearchContext::SelDepth.load(std::memory_order_relaxed);
        const uint64_t nodes = SearchContext::Nodes.load(std::memory_order_relaxed);

        const auto elapsedMs = std::max<int64_t>(
            1,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - searchStart
            ).count()
        );

        const uint64_t nps = nodes * 1000ull / static_cast<uint64_t>(elapsedMs);

        std::cout << "info" << " depth " << depth << " seldepth " << selDepth << " score ";

        if (score > MateThreshold) {
            std::cout << "mate " << ((INF - score) / 2 + 1);
        } else if (score < -MateThreshold) {
            std::cout << "mate " << (-(INF + score) / 2 - 1);
        } else {
            std::cout << "cp " << score;
        }

        std::cout << " nodes " << nodes << " nps " << nps << " time " << elapsedMs << " pv " << MoveToUCI(bestMove) << std::endl;
    }
}

Move Board::FindBestMoveByTime(PieceColor sideToMove, int thinkTimeMS, bool useOpeningBook) const {
    // try opening book first
    if (useOpeningBook) {
        Move bookMove;

        if (OpeningBook::Probe(m_Hash, bookMove)) {
            return bookMove;
        }
    }

    // begin new search
    SearchContext::BeginNew(m_Hash);
    SearchContext::RootSide = sideToMove;

    // collect all moves
    ScoredMove allMoves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    {
        Move legalMoves[MaxLegalMoves];

        getAllLegalMoves(sideToMove, legalMoves, numMoves);

        for (unsigned int i = 0u; i < numMoves; ++i) {
            const Move move = legalMoves[i];
            allMoves[i] = ScoredMove(move, scoreMove(move, sideToMove));
        }
    }

    if (numMoves == 1u) {
        // obviously, no point in wasting time
        return allMoves[0].Move;
    }

    std::sort(allMoves, allMoves + numMoves,
        [](const ScoredMove& a, const ScoredMove& b) {
            return a.Score > b.Score;
        }
    );

    Move bestMove = allMoves[0].Move;
    int bestScore = -INF;

    const auto searchStart = std::chrono::steady_clock::now();

    // cancel search after time limit expires
    std::thread timerThread([thinkTimeMS]() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(thinkTimeMS);

        while (std::chrono::steady_clock::now() < deadline) {
            if (SearchContext::Cancelled.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        SearchContext::Cancelled.store(true, std::memory_order_relaxed);
    });

    std::vector<std::thread> helperThreads = StartHelperThreads(
        [root = *this, sideToMove](int i) { root.searchHelper(sideToMove, i); }
    );

    Move rootMoves[MaxLegalMoves];
    for (unsigned int i = 0u; i < numMoves; ++i) rootMoves[i] = allMoves[i].Move;

    // iterative deepening
    for (int depth = 1; depth <= 96 && !SearchContext::Cancelled.load(std::memory_order_relaxed); ++depth) {
        if (!searchRoot(rootMoves, numMoves, sideToMove, depth, bestMove, bestScore, true)) break;

        PrintUciInfo(depth, bestScore, bestMove, searchStart);
    }

    StopHelperThreads(helperThreads);
    timerThread.join();

    return bestMove;
}

#pragma region Find By Depth

Move Board::FindBestMoveByDepth(PieceColor sideToMove, int targetDepth, bool useOpeningBook) const {
    // try opening book first
    if (useOpeningBook) {
        Move bookMove;

        if (OpeningBook::Probe(m_Hash, bookMove)) {
            return bookMove;
        }
    }

    // begin new search
    SearchContext::BeginNew(m_Hash);
    SearchContext::RootSide = sideToMove;

    // collect all legal moves
    ScoredMove allMoves[MaxLegalMoves];
    unsigned int numMoves = 0u;

    {
        Move legalMoves[MaxLegalMoves];

        getAllLegalMoves(sideToMove, legalMoves, numMoves);

        for (unsigned int i = 0u; i < numMoves; ++i) {
            const Move move = legalMoves[i];
            allMoves[i] = ScoredMove(move, scoreMove(move, sideToMove));
        }
    }

    if (numMoves == 1) {
        // obviously, no point in wasting time
        return allMoves[0].Move;
    }

    std::sort(allMoves, allMoves + numMoves,
        [](const ScoredMove& a, const ScoredMove& b) {
            return a.Score > b.Score;
        }
    );

    Move bestMove = allMoves[0].Move;
    int bestScore = -INF;

    const auto searchStart = std::chrono::steady_clock::now();

    std::vector<std::thread> helperThreads = StartHelperThreads(
        [root = *this, sideToMove](int i) { root.searchHelper(sideToMove, i); }
    );

    Move rootMoves[MaxLegalMoves];
    for (unsigned int i = 0u; i < numMoves; ++i) rootMoves[i] = allMoves[i].Move;

    // iterative deepening
    for (int depth = 1; depth <= targetDepth && !SearchContext::Cancelled.load(std::memory_order_relaxed); ++depth) {
        if (!searchRoot(rootMoves, numMoves, sideToMove, depth, bestMove, bestScore, true)) break;

        PrintUciInfo(depth, bestScore, bestMove, searchStart);
    }

    StopHelperThreads(helperThreads);

    return bestMove;
}

void Board::CancelSearch() {
    SearchContext::Cancelled.store(true, std::memory_order_relaxed);
}

#pragma region Perft

unsigned int Board::numPositions(int depth, PieceColor sideToMove) {
    if (depth <= 0) return 1;

    UninitArray<Move, MaxLegalMoves> moves;
    unsigned int numMoves = 0u;

    getAllLegalMoves(sideToMove, moves, numMoves);

    uint64_t totalPositions = 0ull;

    for (unsigned int i = 0u; i < numMoves; ++i) {
        const Move move = moves[i];

        Board copy = *this;
        copy.doMoveInternal(move);

        totalPositions += copy.numPositions(depth - 1, InvertColor(sideToMove));
    }

    return totalPositions;
}

void Board::Perft(int depth, PieceColor sideToMove) {
    UninitArray<Move, MaxLegalMoves> moves;
    unsigned int numMoves = 0u;

    getAllLegalMoves(sideToMove, moves, numMoves);

    uint64_t totalNodes = 0ull;

    for (unsigned int i = 0u; i < numMoves; ++i) {
        const Move move = moves[i];

        Board copy = *this;
        copy.doMoveInternal(move);

        const uint64_t nodes = copy.numPositions(depth - 1, InvertColor(sideToMove));
        totalNodes += nodes;

        std::cout << MoveToUCI(move) << ": " << nodes << std::endl;
    }

    std::cout << std::endl << "Nodes searched: " << totalNodes << std::endl;
}

#pragma region Debug

uint64_t Board::getCheckers(PieceColor friendlyColor) const {
    const int enemyOff = (friendlyColor == PieceColor::White) * 6;
    const bool isBlack = friendlyColor == PieceColor::Black;
    const int kingSq = MaskToIndex(m_PieceMask[Utils::PieceToBitIdx(PieceType::King, friendlyColor)]);

    uint64_t checkers = 0ull;

    const uint64_t occ = GetOccupancyMap();

    // sliders
    const uint64_t enemyQueens = m_PieceMask[enemyOff + 4];
    const uint64_t enemyRooksQueens = m_PieceMask[enemyOff + 3] | enemyQueens;
    const uint64_t enemyBishopsQueens = m_PieceMask[enemyOff + 1] | enemyQueens;

    checkers |= MagicBitboard::OrthogonalAttacks(kingSq, occ) & enemyRooksQueens;
    checkers |= MagicBitboard::DiagonalAttacks(kingSq, occ) & enemyBishopsQueens;
    checkers |= LUTs::KnightAttacks[kingSq] & m_PieceMask[enemyOff + 2];
    checkers |= LUTs::PawnAttacks[isBlack][kingSq] & m_PieceMask[enemyOff];

    return checkers;
}

void Board::PrintBoard(PieceColor sideToMove) {
    const char* rankSep = "+---+---+---+---+---+---+---+---+";
    const char fileSep = '|';

    std::cout << std::endl;

    for (int rank = 0; rank < Ranks; ++rank) {
        std::cout << rankSep << std::endl;

        for (int file = 0; file < Files; ++file) {
            std::cout << fileSep;

            const Piece pieceAtPos = m_Mailbox[To2DIndex(rank, file)];
            const char c = pieceAtPos.Type == PieceType::None ? ' ' : PieceToChar(pieceAtPos);

            std::cout << ' ' << c << ' ';
        }

        std::cout << fileSep << ' ' << (8 - rank) << std::endl;
    }

    std::cout << rankSep << std::endl;

    std::cout << "  a   b   c   d   e   f   g   h" << std::endl << std::endl;

    std::cout << "Fen: " << GetFen(sideToMove) << std::endl;
    std::cout << "Key: " << std::uppercase << std::hex << m_Hash << std::nouppercase << std::dec << std::endl;
    std::cout << "Checkers: ";

    for (uint64_t tmp = getCheckers(sideToMove); tmp; tmp &= tmp - 1) {
        const int sq = MaskToIndex(tmp);

        const char file = 'a' + ToFile(sq);
        const char rank = '1' + (7 - ToRank(sq));

        std::cout << file << rank << " ";
    }

    std::cout << std::endl;
}

uint16_t Chess::Board::GetMoveCount() const noexcept {
    return GameContext::MoveCount;
}

void Chess::Board::SetCatchAll(bool value) const {
    SearchContext::CatchAllMode = value;
}

bool Chess::Board::GetCatchAll() const noexcept {
    return SearchContext::CatchAllMode;
}

void Chess::Board::SetEngineColor(PieceColor color) const {
    SearchContext::EnginePlayer = color;
}
