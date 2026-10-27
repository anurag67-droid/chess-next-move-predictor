#include "gui/format.hpp"

#include "engine/search.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gui {

std::string formatScore(int score, bool withSign)
{
    char buf[16];
    if (engine::isMateScore(score)) {
        std::snprintf(buf, sizeof(buf), "%sM%d", score < 0 ? "-" : "", std::abs(engine::mateInMoves(score)));
    } else {
        std::snprintf(buf, sizeof(buf), withSign ? "%+.2f" : "%.1f", score / 100.0);
        if (!withSign && buf[0] == '-') return buf + 1; // the bar's colour already shows the side
    }
    return buf;
}

std::string describeScore(int score)
{
    if (engine::isMateScore(score)) {
        const int moves = engine::mateInMoves(score);
        return std::string(moves > 0 ? "White" : "Black") + " mates in " + std::to_string(std::abs(moves));
    }
    const int magnitude = std::abs(score);
    const std::string side = score > 0 ? "White" : "Black";
    if (magnitude < 30) return "Equal position";
    if (magnitude < 100) return side + " is slightly better";
    if (magnitude < 300) return side + " is better";
    return side + " is winning";
}

float evalBarShare(int score)
{
    if (engine::isMateScore(score)) return score > 0 ? 1.f : 0.f;
    const float winChance = 2.f / (1.f + std::exp(-0.00368208f * static_cast<float>(score))) - 1.f;
    return std::clamp(0.5f + 0.5f * winChance, 0.03f, 0.97f);
}

std::string formatNodes(std::uint64_t nodes)
{
    char buf[16];
    if (nodes >= 1'000'000) std::snprintf(buf, sizeof(buf), "%.1fM", nodes / 1e6);
    else if (nodes >= 1'000) std::snprintf(buf, sizeof(buf), "%.0fk", nodes / 1e3);
    else std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(nodes));
    return buf;
}

std::string formatClock(float seconds)
{
    seconds = std::max(seconds, 0.f);
    char buf[16];
    if (seconds < 10.f) {
        std::snprintf(buf, sizeof(buf), "0:%04.1f", seconds);
    } else {
        const int total = static_cast<int>(std::ceil(seconds));
        std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    }
    return buf;
}

} // namespace gui
