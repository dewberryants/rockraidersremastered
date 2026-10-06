#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rr {

enum class BoolTri { False = 0, True = 1, Unset = 2 };

// Parser and lookup for the game's .cfg format (Lego.cfg, level/object configs).
//
// Format: whitespace-separated tokens; ';' starts a comment to end of line. Tokens pair up as
// "key value"; a value of "{" opens a block whose items are one level deeper, and a lone "}"
// closes it. Values cannot contain whitespace (the game uses '_' for spaces in display text).
//
// Lookup ids are "::"-separated paths ("Lego*::Main::LoadScreen"). A key ending in '*' at the
// top level matches any first path component with that prefix. Lookup returns the first match
// in file order, except when the top-level key matched through such a wildcard: then the last
// match wins (this is what the original engine does, so later duplicates override earlier ones).
class Config {
public:
    struct Item {
        std::string key;
        std::string value;
        int depth = 0;
    };
    using ItemRef = int;  // index into items(); -1 = none
    static constexpr ItemRef kNone = -1;

    bool Load(std::string_view path);  // through the VFS
    void Parse(std::string_view text);

    ItemRef Find(std::string_view id) const;
    // Block lookup: returns the first child item of the block named by id, or kNone.
    ItemRef FindArray(std::string_view id) const;
    // Next item at the same depth inside the same block, or kNone.
    ItemRef Next(ItemRef item) const;

    std::optional<std::string_view> GetString(std::string_view id) const;
    std::string GetString(std::string_view id, std::string_view fallback) const;
    BoolTri GetBool(std::string_view id) const;
    bool GetBool(std::string_view id, bool fallback) const;
    std::optional<float> GetFloat(std::string_view id) const;
    std::optional<int> GetInt(std::string_view id) const;
    float GetAngle(std::string_view id) const;  // degrees in file -> radians
    bool GetRGB(std::string_view id, float& r, float& g, float& b) const;

    const Item& item(ItemRef ref) const { return items_[static_cast<size_t>(ref)]; }
    const std::vector<Item>& items() const { return items_; }
    bool empty() const { return items_.empty(); }

    // Joins parts with "::" (Config_BuildStringID).
    template <typename... Parts>
    static std::string Id(std::string_view first, const Parts&... rest) {
        std::string out(first);
        ((out += "::", out += std::string_view(rest)), ...);
        return out;
    }

private:
    std::vector<Item> items_;
};

BoolTri ParseBool(std::string_view s);

// Splits on a separator string, keeping empty fields (Util_Tokenise semantics).
std::vector<std::string_view> SplitTokens(std::string_view s, std::string_view sep);

}  // namespace rr
