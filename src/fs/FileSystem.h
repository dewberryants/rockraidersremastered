#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fs/Wad.h"

namespace rr {

// Virtual file system matching the original lookup rules:
//  - archives "<program><N>.wad" (N = 0..9) are mounted from the game directory;
//  - reads look in the archives first, highest N first (so LegoRR1.wad overrides LegoRR0.wad);
//  - otherwise the loose file "<game>\Data\<path>" is used;
//  - writes always go to the loose Data directory.
// Paths are game-relative ("Levels\GameLevels\Level01\01.txt"), case-insensitive, '\' or '/'.
class FileSystem {
public:
    bool Mount(const std::filesystem::path& gameDir, std::string_view programName = "LegoRR");

    std::optional<std::vector<uint8_t>> ReadAll(std::string_view path) const;
    std::optional<std::string> ReadText(std::string_view path) const;
    bool Exists(std::string_view path) const;
    bool ExistsInWad(std::string_view path) const;

    // Loose-file location for a game path (case-insensitive match on disk); empty if absent.
    std::filesystem::path FindOnDisk(std::string_view path) const;
    // Loose-file location for writing (directories are created).
    std::filesystem::path PathForWrite(std::string_view path) const;

    const std::filesystem::path& GameDir() const { return gameDir_; }
    const std::filesystem::path& DataDir() const { return dataDir_; }

private:
    std::filesystem::path gameDir_;
    std::filesystem::path dataDir_;
    std::vector<std::unique_ptr<Wad>> wads_;  // ordered highest priority first
};

FileSystem& Fs();

}  // namespace rr
