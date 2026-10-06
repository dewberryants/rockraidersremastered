#include "fs/Wad.h"

#include <cstring>

#include "core/Log.h"
#include "fs/Path.h"

namespace rr {

namespace {
uint32_t ReadU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

bool Wad::Open(const std::filesystem::path& path) {
    path_ = path;
    stream_.open(path, std::ios::binary);
    if (!stream_) return false;
    stream_.seekg(0, std::ios::end);
    const uint64_t fileSize = static_cast<uint64_t>(stream_.tellg());
    stream_.seekg(0);

    uint8_t header[8];
    if (!stream_.read(reinterpret_cast<char*>(header), 8) || std::memcmp(header, "WWAD", 4) != 0) {
        RR_ERROR("%s is not a WWAD archive", path.string().c_str());
        return false;
    }
    const uint32_t count = ReadU32(header + 4);

    // Read the two name tables: grow the buffer until 2*count NUL-terminated strings are present.
    std::vector<char> dir;
    std::vector<std::string> names;
    names.reserve(count);
    size_t pos = 0, stringsLeft = size_t(count) * 2;
    while (stringsLeft > 0) {
        const void* nul = pos < dir.size() ? std::memchr(dir.data() + pos, 0, dir.size() - pos) : nullptr;
        if (!nul) {
            const size_t old = dir.size();
            dir.resize(old + 64 * 1024);
            stream_.read(dir.data() + old, 64 * 1024);
            dir.resize(old + static_cast<size_t>(stream_.gcount()));
            stream_.clear();
            if (dir.size() == old) {
                RR_ERROR("%s: truncated name table", path.string().c_str());
                return false;
            }
            continue;
        }
        const size_t end = static_cast<const char*>(nul) - dir.data();
        if (names.size() < count) names.emplace_back(dir.data() + pos, end - pos);
        pos = end + 1;
        --stringsLeft;
    }

    std::vector<uint8_t> table(size_t(count) * 16);
    stream_.seekg(static_cast<std::streamoff>(8 + pos));
    if (!stream_.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size()))) {
        RR_ERROR("%s: truncated entry table", path.string().c_str());
        return false;
    }

    entries_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        Entry& e = entries_[i];
        const uint8_t* p = table.data() + size_t(i) * 16;
        e.name = std::move(names[i]);
        e.flags = ReadU32(p);
        e.size = ReadU32(p + 4);
        e.packedSize = ReadU32(p + 8);
        e.offset = ReadU32(p + 12);
        if (uint64_t(e.offset) + e.size > fileSize) {
            RR_WARN("%s: entry %s exceeds archive size", path.string().c_str(), e.name.c_str());
            continue;
        }
        index_[NormalizeGamePath(e.name)] = i;
    }
    RR_INFO("Loaded %s (%u entries)", path.filename().string().c_str(), count);
    return true;
}

const Wad::Entry* Wad::Find(const std::string& normalizedKey) const {
    auto it = index_.find(normalizedKey);
    return it == index_.end() ? nullptr : &entries_[it->second];
}

bool Wad::Read(const Entry& entry, std::vector<uint8_t>& out) const {
    if (entry.flags != 1) {
        // Retail archives only contain stored entries; other flag values were never shipped.
        RR_ERROR("%s: unsupported WAD entry flags %u", entry.name.c_str(), entry.flags);
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    out.resize(entry.size);
    stream_.clear();
    stream_.seekg(entry.offset);
    return static_cast<bool>(stream_.read(reinterpret_cast<char*>(out.data()), entry.size));
}

}  // namespace rr
