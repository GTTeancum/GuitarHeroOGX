#pragma once

#include <optional>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace ghogx::camera {

// GH2 PS2 SameTargets 2664D0..26659C compares list size, then searches the
// other list for each object-pointer/subpart identity. Empty lists match;
// null slots participate in size and identity. Authored order is irrelevant.
template <class Identities>
bool same_target_identities(const Identities& a, const Identities& b) {
    if (a.size() != b.size()) return false;
    for (const auto& id : a)
        if (std::find(b.begin(), b.end(), id) == b.end()) return false;
    return true;
}

// The caller must test exact object existence, without SubPart's entity-root
// fallback. This lowers a source {if_else {exists primary} primary fallback}.
template <class ExactExists>
std::optional<std::pair<std::string, std::string>> reference_fallback(
    std::string_view entity, std::string_view part, std::string_view fallback,
    ExactExists&& exact_exists) {
    if (fallback.empty() || exact_exists(entity, part)) return std::nullopt;
    const auto separator = fallback.find("::");
    if (separator == std::string_view::npos || separator == 0 || separator + 2 == fallback.size())
        return std::nullopt;
    return std::pair{std::string(fallback.substr(0, separator)),
                     std::string(fallback.substr(separator + 2))};
}

}  // namespace ghogx::camera
