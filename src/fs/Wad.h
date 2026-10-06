#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rr {

// A "WWAD" archive as shipped with the game (LegoRR0.wad, LegoRR1.wad).
// Layout: "WWAD", u32 count, count relative names, count original build paths,
// then count x {u32 flags, u32 size, u32 packedSize, u32 offset}. All retail entries are stored.
class Wad {
public:
    struct Entry {
        std::string name;  // as stored, e.g. "Levels\GameLevels\Level01\01.txt"
        uint32_t flags = 0;
        uint32_t size = 0;
        uint32_t packedSize = 0;
        uint32_t offset = 0;
    };

    bool Open(const std::filesystem::path& path);
    const Entry* Find(const std::string& normalizedKey) const;
    bool Read(const Entry& entry, std::vector<uint8_t>& out) const;
    const std::vector<Entry>& Entries() const { return entries_; }
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;  // normalized key -> entry
    mutable std::ifstream stream_;
    mutable std::mutex mutex_;
};

}  // namespace rr
