#pragma once

#include "predictor/player_book.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gui {

// Reads a player's PGN file on a background thread; big files take a moment.
class PlayerWorker {
public:
    struct Loaded {
        std::filesystem::path path;
        std::shared_ptr<const predictor::PlayerBook> book; // null on failure
        std::string error;
    };

    ~PlayerWorker();

    // Ignored while a file is already loading.
    void load(const std::filesystem::path& path);
    bool busy() const { return m_running; }
    const std::filesystem::path& loadingPath() const { return m_path; }
    // The result of the last load, once.
    std::optional<Loaded> takeLoaded();

private:
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::filesystem::path m_path;
    std::mutex m_mutex;
    std::optional<Loaded> m_loaded;
};

// PGN files in the players folder that tools/fetch_games.py downloads into.
std::vector<std::filesystem::path> findPlayerFiles();

} // namespace gui
