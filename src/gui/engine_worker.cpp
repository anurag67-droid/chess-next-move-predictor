#include "gui/engine_worker.hpp"

namespace gui {

void EngineWorker::start(Task task, const chess::Board& board, engine::SearchLimits limits)
{
    cancel();
    m_task = task;
    m_stop = false;
    m_running = true;
    limits.stopSignal = &m_stop;

    m_thread = std::thread([this, task, board, limits] {
        const auto onIteration = [this](const engine::SearchResult& r) {
            std::lock_guard lock(m_mutex);
            m_latest = r;
        };
        const auto result = m_searcher.search(board, limits, onIteration);
        {
            std::lock_guard lock(m_mutex);
            if (result.depth > 0) m_latest = result;
            m_finished = Finished{task, board.hash(), result};
        }
        m_running = false;
    });
}

void EngineWorker::cancel()
{
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard lock(m_mutex);
    m_latest = {};
    m_finished.reset();
}

engine::SearchResult EngineWorker::latest() const
{
    std::lock_guard lock(m_mutex);
    return m_latest;
}

std::optional<EngineWorker::Finished> EngineWorker::takeFinished()
{
    std::lock_guard lock(m_mutex);
    auto finished = std::move(m_finished);
    m_finished.reset();
    return finished;
}

} // namespace gui
