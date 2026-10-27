#pragma once

#include <filesystem>
#include <string>

namespace gui {

// Resolves a file in the assets folder, whether the app is started from the
// build directory, the project root, or anywhere else on the build machine.
inline std::string assetPath(const std::string& name)
{
    for (const char* dir : {CHESS_ASSETS_DIR, "assets", "../assets"}) {
        const auto path = std::filesystem::path(dir) / name;
        if (std::filesystem::exists(path)) return path.string();
    }
    return name;
}

} // namespace gui
