#include "config/Config.h"

#include <cmath>
#include <cstdlib>

#include "core/Log.h"
#include "fs/FileSystem.h"
#include "fs/Path.h"

namespace rr {

BoolTri ParseBool(std::string_view s) {
    if (EqualsNoCase(s, "YES") || EqualsNoCase(s, "TRUE") || EqualsNoCase(s, "ON")) return BoolTri::True;
    if (EqualsNoCase(s, "NO") || EqualsNoCase(s, "FALSE") || EqualsNoCase(s, "OFF")) return BoolTri::False;
    return BoolTri::Unset;
}

std::vector<std::string_view> SplitTokens(std::string_view s, std::string_view sep) {
    std::vector<std::string_view> out;
    if (s.empty()) return out;
    size_t start = 0;
    for (;;) {
        const size_t hit = s.find(sep, start);
        if (hit == std::string_view::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, hit - start));
        start = hit + sep.size();
    }
}

bool Config::Load(std::string_view path) {
    auto text = Fs().ReadText(path);
    if (!text) {
        RR_WARN("Config file %.*s not found", int(path.size()), path.data());
        return false;
    }
    Parse(*text);
    return true;
}

void Config::Parse(std::string_view text) {
    items_.clear();
    Item pending;
    bool havePendingKey = false;

    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == ';') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        size_t end = i;
        while (end < text.size() && text[end] != ';' && text[end] != ' ' && text[end] != '\t' &&
               text[end] != '\r' && text[end] != '\n')
            ++end;
        const std::string_view token = text.substr(i, end - i);
        i = end;

        if (token == "}") {
            --pending.depth;  // closing a block lowers the depth of everything that follows
        } else if (!havePendingKey) {
            pending.key = std::string(token);
            havePendingKey = true;
        } else {
            pending.value = std::string(token);
            const int depth = pending.depth;
            items_.push_back(std::move(pending));
            pending = Item{};
            pending.depth = (token == "{") ? depth + 1 : depth;
            havePendingKey = false;
        }
    }
}

namespace {
// Top-level wildcard: key "Lego*" matches any component starting with "Lego".
bool WildcardMatch(std::string_view component, std::string_view key) {
    const size_t star = key.find('*');
    if (star == std::string_view::npos) return false;
    return StartsWithNoCase(component, key.substr(0, star));
}
}  // namespace

Config::ItemRef Config::Find(std::string_view id) const {
    const std::vector<std::string_view> parts = SplitTokens(id, "::");
    const int n = static_cast<int>(parts.size());
    if (n == 0) return kNone;

    ItemRef result = kNone;
    for (int idx = 0; idx < static_cast<int>(items_.size()); ++idx) {
        const Item& it = items_[idx];
        if (it.depth != n - 1) continue;
        const bool selfWild = (n == 1) && WildcardMatch(parts[0], it.key);
        if (!selfWild && !EqualsNoCase(parts[n - 1], it.key)) continue;

        // Walk back through the enclosing blocks: the nearest preceding item one level up
        // is the parent block header.
        bool usedWildcard = false;
        int level = n - 1;
        for (int anc = idx - 1; anc >= 0 && level > 0; --anc) {
            const Item& a = items_[anc];
            if (a.depth != level - 1) continue;
            if (level == 1 && WildcardMatch(parts[0], a.key)) {
                usedWildcard = true;
            } else if (!EqualsNoCase(parts[level - 1], a.key)) {
                break;
            }
            --level;
        }
        if (level != 0) continue;
        result = idx;
        if (!usedWildcard) break;
    }
    return result;
}

Config::ItemRef Config::FindArray(std::string_view id) const {
    const ItemRef ref = Find(id);
    if (ref == kNone || ref + 1 >= static_cast<int>(items_.size())) return kNone;
    return items_[ref + 1].depth > items_[ref].depth ? ref + 1 : kNone;
}

Config::ItemRef Config::Next(ItemRef ref) const {
    if (ref == kNone) return kNone;
    const int depth = items_[ref].depth;
    for (int i = ref + 1; i < static_cast<int>(items_.size()); ++i) {
        if (items_[i].depth == depth) return i;
        if (items_[i].depth < depth) return kNone;
    }
    return kNone;
}

std::optional<std::string_view> Config::GetString(std::string_view id) const {
    const ItemRef ref = Find(id);
    if (ref == kNone) return std::nullopt;
    return std::string_view(items_[ref].value);
}

std::string Config::GetString(std::string_view id, std::string_view fallback) const {
    auto v = GetString(id);
    return std::string(v ? *v : fallback);
}

BoolTri Config::GetBool(std::string_view id) const {
    auto v = GetString(id);
    return v ? ParseBool(*v) : BoolTri::Unset;
}

bool Config::GetBool(std::string_view id, bool fallback) const {
    const BoolTri b = GetBool(id);
    return b == BoolTri::Unset ? fallback : b == BoolTri::True;
}

std::optional<float> Config::GetFloat(std::string_view id) const {
    auto v = GetString(id);
    if (!v) return std::nullopt;
    return static_cast<float>(std::atof(std::string(*v).c_str()));
}

std::optional<int> Config::GetInt(std::string_view id) const {
    auto v = GetString(id);
    if (!v) return std::nullopt;
    return std::atoi(std::string(*v).c_str());
}

float Config::GetAngle(std::string_view id) const {
    const float degrees = GetFloat(id).value_or(0.0f);
    return degrees / 360.0f * 6.2831855f;
}

bool Config::GetRGB(std::string_view id, float& r, float& g, float& b) const {
    auto v = GetString(id);
    if (!v) return false;
    const auto parts = SplitTokens(*v, ":");
    if (parts.size() != 3) return false;
    r = std::atoi(std::string(parts[0]).c_str()) / 255.0f;
    g = std::atoi(std::string(parts[1]).c_str()) / 255.0f;
    b = std::atoi(std::string(parts[2]).c_str()) / 255.0f;
    return true;
}

}  // namespace rr
