#include "fs/Path.h"

#include <cctype>
#include <vector>

namespace rr {

std::string ToLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool EqualsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool StartsWithNoCase(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && EqualsNoCase(s.substr(0, prefix.size()), prefix);
}

std::string CleanGamePath(std::string_view path) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '\\' || path[i] == '/') {
            std::string_view part = path.substr(start, i - start);
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            start = i + 1;
        }
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '\\';
        out += parts[i];
    }
    return out;
}

std::string NormalizeGamePath(std::string_view path) { return ToLower(CleanGamePath(path)); }

}  // namespace rr
