#pragma once

#include "engine/search.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace gui {

// Runs engine searches on a background thread so the UI never freezes.
class EngineWorker {
public:
    enum class Task { Analyse, Play };

    struct Finished {
        Task task;
        std::uint64_t positionKey;
        engine::SearchResult result;
    };

    ~EngineWorker() { cancel(); }

    // Cancels any running search and starts a new one on a copy of `board`.
    void start(Task task, const chess::Board& board, engine::SearchLimits limits);
    // Aborts the search and throws its result away.
    void cancel();
    // Asks the search to finish now; its result is still delivered by takeFinished().
    void finishNow() { m_stop = true; }
    // Drops what the engine has learned, e.g. for a new game. Must not be called while busy.
    void clearMemory() { m_searcher.clear(); }

    bool busy() const { return m_running; }
    Task task() const { return m_task; }
    // Result of the deepest completed iteration of the current search (depth 0 if none yet).
    engine::SearchResult latest() const;
    std::optional<Finished> takeFinished();

private:
    engine::Searcher m_searcher;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    Task m_task = Task::Analyse;

    mutable std::mutex m_mutex;
    engine::SearchResult m_latest;
    std::optional<Finished> m_finished;
};

} // namespace gui
