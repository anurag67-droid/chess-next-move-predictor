#include "predictor/move_net.hpp"

#include "core/game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace predictor {
namespace {

// Must match ml/chessml/encoding.py.
constexpr int kPlanes = 18;     // 12 pieces, 4 castling rights, en passant, a plane of ones
constexpr int kMovePlanes = 73; // per from-square: 56 queen-like moves, 8 knight jumps, 9 under-promotions
constexpr int kEloMin = 600, kEloStep = 100;

class Reader {
public:
    explicit Reader(const std::filesystem::path& path) : m_file(path, std::ios::binary), m_name(path.string())
    {
        if (!m_file) throw std::runtime_error("Cannot open " + m_name);
    }

    void read(void* data, std::size_t bytes)
    {
        if (!m_file.read(static_cast<char*>(data), static_cast<std::streamsize>(bytes)))
            throw std::runtime_error(m_name + " is cut short");
    }
    int int32()
    {
        unsigned char b[4];
        read(b, 4);
        return static_cast<std::int32_t>(b[0] | b[1] << 8 | b[2] << 16 | static_cast<std::uint32_t>(b[3]) << 24);
    }
    // Little-endian IEEE floats, which is what every platform this builds on uses.
    std::vector<float> floats(std::size_t count)
    {
        std::vector<float> out(count);
        read(out.data(), count * sizeof(float));
        return out;
    }
    bool atEnd() { return m_file.peek() == std::ifstream::traits_type::eof(); }
    const std::string& name() const { return m_name; }

private:
    std::ifstream m_file;
    std::string m_name;
};

void relu(std::vector<float>& x)
{
    for (float& v : x) v = std::max(0.f, v);
}

} // namespace

std::vector<float> MoveNet::apply(const Conv& conv, const std::vector<float>& in)
{
    const int k = conv.size, half = k / 2;
    std::vector<float> out(64 * static_cast<std::size_t>(conv.out));
    for (int sq = 0; sq < 64; ++sq) {
        const int rank = sq / 8, file = sq % 8;
        float* o = &out[static_cast<std::size_t>(sq) * conv.out];
        std::copy(conv.bias.begin(), conv.bias.end(), o);
        for (int ky = 0; ky < k; ++ky) {
            const int r = rank + ky - half;
            if (r < 0 || r > 7) continue;
            for (int kx = 0; kx < k; ++kx) {
                const int f = file + kx - half;
                if (f < 0 || f > 7) continue;
                const float* x = &in[static_cast<std::size_t>(r * 8 + f) * conv.in];
                const float* w = &conv.weight[static_cast<std::size_t>(ky * k + kx) * conv.in * conv.out];
                for (int ci = 0; ci < conv.in; ++ci, w += conv.out) {
                    const float v = x[ci];
                    if (v == 0.f) continue; // after a ReLU about half the inputs are zero
                    for (int co = 0; co < conv.out; ++co) o[co] += v * w[co];
                }
            }
        }
    }
    return out;
}

std::size_t MoveNet::parameterCount() const
{
    std::size_t n = m_context.weight.size() + m_context.bias.size();
    for (const auto& e : m_embeddings) n += e.size();
    const auto conv = [](const Conv& c) { return c.weight.size() + c.bias.size(); };
    n += conv(m_stem) + conv(m_policyConv) + conv(m_policyOut);
    for (const auto& b : m_blocks) n += conv(b.conv1) + conv(b.conv2) + b.film.weight.size() + b.film.bias.size();
    return n;
}

MoveNet MoveNet::fromFile(const std::filesystem::path& path)
{
    Reader in(path);
    char magic[8];
    in.read(magic, sizeof(magic));
    if (std::memcmp(magic, "MOVENET1", sizeof(magic)) != 0)
        throw std::runtime_error(in.name() + " is not a MoveNet export (see python -m chessml.export)");

    MoveNet net;
    const int c = net.m_channels = in.int32();
    const int blocks = in.int32();
    const int d = net.m_contextDim = in.int32();
    const int eloBuckets = in.int32(), speeds = in.int32(), clockBuckets = in.int32();
    const int planes = in.int32(), movePlanes = in.int32(), moves = in.int32();
    if (c <= 0 || c > 1024 || blocks < 0 || blocks > 64 || d <= 0 || d > 1024 || eloBuckets <= 0 || speeds != 4
        || clockBuckets <= 0 || planes != kPlanes || movePlanes != kMovePlanes || moves <= 0 || moves > 4096)
        throw std::runtime_error(in.name() + " was exported with a different input or move encoding");
    net.m_buckets = {eloBuckets, eloBuckets, speeds, clockBuckets};

    std::vector<int> table(4 * static_cast<std::size_t>(moves));
    for (int& v : table) v = in.int32();
    net.m_moveLookup.assign(64 * 64 * 5, -1);
    for (int i = 0; i < moves; ++i) {
        const NetMove m{table[i], table[moves + i], table[2 * moves + i], table[3 * moves + i]};
        if (m.from < 0 || m.from > 63 || m.to < 0 || m.to > 63 || m.promotion < 0 || m.promotion > 4 || m.cell < 0
            || m.cell >= kMovePlanes * 64)
            throw std::runtime_error(in.name() + " has a bad move table");
        net.m_moves.push_back(m);
        net.m_moveLookup[(m.from * 64 + m.to) * 5 + m.promotion] = i;
    }

    for (int i = 0; i < 4; ++i) net.m_embeddings[i] = in.floats(static_cast<std::size_t>(net.m_buckets[i]) * d);

    const auto linear = [&](int inputs, int outputs) {
        Linear l{inputs, outputs, in.floats(static_cast<std::size_t>(outputs) * inputs), {}};
        l.bias = in.floats(outputs);
        return l;
    };
    // PyTorch stores a convolution as [out][in][ky][kx]; reorder to [ky][kx][in][out] for apply().
    const auto conv = [&](int inputs, int outputs, int size) {
        const auto torch = in.floats(static_cast<std::size_t>(outputs) * inputs * size * size);
        Conv cv{inputs, outputs, size, std::vector<float>(torch.size()), in.floats(outputs)};
        for (int o = 0; o < outputs; ++o)
            for (int i = 0; i < inputs; ++i)
                for (int k = 0; k < size * size; ++k)
                    cv.weight[(static_cast<std::size_t>(k) * inputs + i) * outputs + o]
                        = torch[(static_cast<std::size_t>(o) * inputs + i) * size * size + k];
        return cv;
    };

    net.m_context = linear(d, d);
    net.m_stem = conv(kPlanes, c, 3);
    for (int b = 0; b < blocks; ++b) {
        Block block;
        block.conv1 = conv(c, c, 3);
        block.conv2 = conv(c, c, 3);
        block.film = linear(d, 2 * c);
        net.m_blocks.push_back(std::move(block));
    }
    net.m_policyConv = conv(c, c, 3);
    net.m_policyOut = conv(c, kMovePlanes, 1);
    if (!in.atEnd()) throw std::runtime_error(in.name() + " has unexpected data at the end");
    return net;
}

std::vector<float> MoveNet::context(const NetContext& ctx) const
{
    const int eloBucket = std::clamp((ctx.elo - kEloMin) / kEloStep, 0, m_buckets[0] - 1);
    const int oppBucket = std::clamp((ctx.opponentElo - kEloMin) / kEloStep, 0, m_buckets[1] - 1);
    int speed = 3; // classical, also for correspondence and unknown
    if (ctx.speed == Speed::Bullet) speed = 0;
    else if (ctx.speed == Speed::Blitz) speed = 1;
    else if (ctx.speed == Speed::Rapid) speed = 2;
    const int unknownClock = m_buckets[3] - 1;
    const int clock = ctx.clockShare ? std::clamp(static_cast<int>(*ctx.clockShare * 10.), 0, unknownClock - 1)
                                     : unknownClock;

    std::vector<float> sum(m_contextDim, 0.f);
    const int buckets[4] = {eloBucket, oppBucket, speed, clock};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < m_contextDim; ++j) sum[j] += m_embeddings[i][static_cast<std::size_t>(buckets[i]) * m_contextDim + j];
    relu(sum);

    std::vector<float> out(m_context.bias);
    for (int o = 0; o < m_context.out; ++o)
        for (int i = 0; i < m_context.in; ++i) out[o] += m_context.weight[static_cast<std::size_t>(o) * m_context.in + i] * sum[i];
    relu(out);
    return out;
}

std::vector<float> MoveNet::policy(const std::vector<float>& planes, const std::vector<float>& ctx) const
{
    auto x = apply(m_stem, planes);
    relu(x);
    const int c = m_channels;
    std::vector<float> film(2 * static_cast<std::size_t>(c));
    for (const Block& block : m_blocks) {
        for (int o = 0; o < 2 * c; ++o) {
            float v = block.film.bias[o];
            for (int i = 0; i < m_contextDim; ++i) v += block.film.weight[static_cast<std::size_t>(o) * m_contextDim + i] * ctx[i];
            film[o] = v;
        }
        auto y = apply(block.conv1, x);
        for (int sq = 0; sq < 64; ++sq)
            for (int ch = 0; ch < c; ++ch) {
                float& v = y[static_cast<std::size_t>(sq) * c + ch];
                v = std::max(0.f, v * (1.f + film[ch]) + film[c + ch]);
            }
        y = apply(block.conv2, y);
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = std::max(0.f, x[i] + y[i]);
    }
    auto p = apply(m_policyConv, x);
    relu(p);
    return apply(m_policyOut, p); // [square][move plane]
}

std::vector<MoveProbability> MoveNet::predict(const chess::Board& board, const NetContext& ctx) const
{
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    if (legal.empty()) return {};

    // The board from the side to move's point of view: Black's moves are mirrored top to bottom.
    const chess::Color us = board.sideToMove();
    const bool mirror = us == chess::Color::BLACK;
    const auto view = [mirror](int sq) { return mirror ? sq ^ 56 : sq; };

    std::vector<float> planes(64 * kPlanes, 0.f);
    for (int sq = 0; sq < 64; ++sq) {
        const chess::Piece piece = board.at<chess::Piece>(chess::Square(sq));
        if (piece == chess::Piece::NONE) continue;
        const int plane = static_cast<int>(piece.type()) + (piece.color() == us ? 0 : 6);
        planes[static_cast<std::size_t>(view(sq)) * kPlanes + plane] = 1.f;
    }
    const auto rights = board.castlingRights();
    using Side = chess::Board::CastlingRights::Side;
    const bool castling[4] = {rights.has(us, Side::KING_SIDE), rights.has(us, Side::QUEEN_SIDE),
                              rights.has(~us, Side::KING_SIDE), rights.has(~us, Side::QUEEN_SIDE)};
    for (int i = 0; i < 4; ++i)
        if (castling[i])
            for (int sq = 0; sq < 64; ++sq) planes[static_cast<std::size_t>(sq) * kPlanes + 12 + i] = 1.f;
    // Only an en passant capture that is actually legal counts (as python-chess' has_legal_en_passant).
    for (const auto& move : legal)
        if (move.typeOf() == chess::Move::ENPASSANT) {
            planes[static_cast<std::size_t>(40 + move.to().file()) * kPlanes + 16] = 1.f;
            break;
        }
    for (int sq = 0; sq < 64; ++sq) planes[static_cast<std::size_t>(sq) * kPlanes + 17] = 1.f;

    const auto scores = policy(planes, context(ctx));

    std::vector<MoveProbability> out;
    std::vector<float> logits;
    for (const auto& move : legal) {
        const int from = view(move.from().index());
        const int to = view(core::targetSquare(move).index());
        int promotion = 0;
        if (move.typeOf() == chess::Move::PROMOTION && move.promotionType() != chess::PieceType::QUEEN)
            promotion = static_cast<int>(move.promotionType()) + 1; // python-chess numbering: knight 2 ... rook 4
        const int index = m_moveLookup[(from * 64 + to) * 5 + promotion];
        if (index < 0) continue; // cannot happen for a legal move
        const int cell = m_moves[index].cell; // plane * 64 + from-square
        out.push_back({move, 0.});
        logits.push_back(scores[static_cast<std::size_t>(cell % 64) * kMovePlanes + cell / 64]);
    }

    const float top = *std::max_element(logits.begin(), logits.end());
    double total = 0.;
    for (std::size_t i = 0; i < out.size(); ++i) total += out[i].probability = std::exp(logits[i] - top);
    for (auto& m : out) m.probability /= total;
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.probability > b.probability; });
    return out;
}

std::vector<MoveProbability> combine(const BookLookup& book, const std::vector<MoveProbability>& network, double alpha)
{
    std::vector<MoveProbability> out;
    out.reserve(network.size());
    for (const auto& m : network) {
        int games = 0;
        for (const auto& stat : book.moves)
            if (stat.move == m.move) games = stat.games;
        out.push_back({m.move, (games + alpha * m.probability) / (book.total + alpha)});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.probability > b.probability; });
    return out;
}

} // namespace predictor
