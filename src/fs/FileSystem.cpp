#include "fs/FileSystem.h"

#include <fstream>

#include "core/Log.h"
#include "fs/Path.h"

namespace fsys = std::filesystem;

namespace rr {

FileSystem& Fs() {
    static FileSystem instance;
    return instance;
}

bool FileSystem::Mount(const fsys::path& gameDir, std::string_view programName) {
    gameDir_ = gameDir;
    dataDir_ = gameDir / "Data";
    wads_.clear();
    for (int n = 9; n >= 0; --n) {
        const fsys::path wadPath = gameDir / (std::string(programName) + std::to_string(n) + ".wad");
        std::error_code ec;
        if (!fsys::exists(wadPath, ec)) continue;
        auto wad = std::make_unique<Wad>();
        if (wad->Open(wadPath)) wads_.push_back(std::move(wad));
    }
    if (wads_.empty() && !fsys::is_directory(dataDir_)) {
        RR_ERROR("No %s*.wad archives or Data directory found in %s", std::string(programName).c_str(),
                 gameDir.string().c_str());
        return false;
    }
    return true;
}

bool FileSystem::ExistsInWad(std::string_view path) const {
    const std::string key = NormalizeGamePath(path);
    for (const auto& wad : wads_)
        if (wad->Find(key)) return true;
    return false;
}

fsys::path FileSystem::FindOnDisk(std::string_view path) const {
    const std::string clean = CleanGamePath(path);
    std::error_code ec;
    fsys::path direct = dataDir_ / fsys::path(clean).make_preferred();
#ifdef _WIN32
    if (fsys::is_regular_file(direct, ec)) return direct;
    return {};
#else
    if (fsys::is_regular_file(direct, ec)) return direct;
    // Case-insensitive walk for case-sensitive file systems.
    fsys::path cur = dataDir_;
    size_t start = 0;
    while (start <= clean.size()) {
        size_t end = clean.find('\\', start);
        if (end == std::string::npos) end = clean.size();
        const std::string part = clean.substr(start, end - start);
        bool found = false;
        for (const auto& entry : fsys::directory_iterator(cur, ec)) {
            if (EqualsNoCase(entry.path().filename().string(), part)) {
                cur = entry.path();
                found = true;
                break;
            }
        }
        if (!found) return {};
        start = end + 1;
    }
    return fsys::is_regular_file(cur, ec) ? cur : fsys::path{};
#endif
}

fsys::path FileSystem::PathForWrite(std::string_view path) const {
    fsys::path target = dataDir_ / fsys::path(CleanGamePath(path)).make_preferred();
    std::error_code ec;
    fsys::create_directories(target.parent_path(), ec);
    return target;
}

std::optional<std::vector<uint8_t>> FileSystem::ReadAll(std::string_view path) const {
    const std::string key = NormalizeGamePath(path);
    for (const auto& wad : wads_) {
        if (const Wad::Entry* e = wad->Find(key)) {
            std::vector<uint8_t> data;
            if (wad->Read(*e, data)) return data;
            return std::nullopt;
        }
    }
    const fsys::path disk = FindOnDisk(path);
    if (disk.empty()) return std::nullopt;
    std::ifstream in(disk, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::optional<std::string> FileSystem::ReadText(std::string_view path) const {
    auto bytes = ReadAll(path);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

bool FileSystem::Exists(std::string_view path) const { return ExistsInWad(path) || !FindOnDisk(path).empty(); }

}  // namespace rr
