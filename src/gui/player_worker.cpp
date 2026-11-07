#include "gui/player_worker.hpp"

#include <algorithm>
#include <exception>
#include <system_error>

namespace gui {

PlayerWorker::~PlayerWorker()
{
    if (m_thread.joinable()) m_thread.join();
}

void PlayerWorker::load(const std::filesystem::path& path)
{
    if (m_running) return;
    if (m_thread.joinable()) m_thread.join();
    m_path = path;
    m_running = true;
    m_thread = std::thread([this, path] {
        Loaded loaded{path, nullptr, {}};
        try {
            loaded.book = std::make_shared<const predictor::PlayerBook>(predictor::PlayerBook::fromPgnFile(path));
        } catch (const std::exception& e) {
            loaded.error = e.what();
        }
        {
            std::lock_guard lock(m_mutex);
            m_loaded = std::move(loaded);
        }
        m_running = false;
    });
}

std::optional<PlayerWorker::Loaded> PlayerWorker::takeLoaded()
{
    std::lock_guard lock(m_mutex);
    auto loaded = std::move(m_loaded);
    m_loaded.reset();
    return loaded;
}

std::vector<std::filesystem::path> findPlayerFiles()
{
    std::vector<std::filesystem::path> files;
    for (const char* dir : {CHESS_PLAYERS_DIR, "players", "../players"}) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".pgn") continue;
            const auto path = std::filesystem::weakly_canonical(entry.path(), ec);
            if (std::find(files.begin(), files.end(), path) == files.end()) files.push_back(path);
        }
    }
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.filename() < b.filename(); });
    return files;
}

std::filesystem::path findModelFile()
{
    std::filesystem::path any;
    for (const char* dir : {CHESS_MODELS_DIR, "ml/models", "../ml/models", "models"}) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".movenet") continue;
            if (entry.path().filename() == "medium.movenet") return entry.path();
            if (any.empty()) any = entry.path();
        }
    }
    return any;
}

} // namespace gui
