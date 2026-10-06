#pragma once

#include <string>
#include <string_view>

namespace rr {

// Normalizes a game-relative path to the lookup key form used everywhere in the VFS:
// backslash separators, no leading separator, "." and ".." segments resolved, lower case.
// The original engine resolved names against "<game>\Data" with _fullpath and compared
// case-insensitively, which this reproduces.
std::string NormalizeGamePath(std::string_view path);

// Same as NormalizeGamePath but keeps the original letter case (for disk lookups and display).
std::string CleanGamePath(std::string_view path);

bool EqualsNoCase(std::string_view a, std::string_view b);
bool StartsWithNoCase(std::string_view s, std::string_view prefix);
std::string ToLower(std::string_view s);

}  // namespace rr
