#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ghogx::camera {

// GH2 USA 260110: append each requested category separately. Retry that
// category only when its unused eligible pass collected nothing. 260348:
// select the first cumulative threshold >= RandomFloat(0, total), mark used.
// Keep authored order and float accumulation; do not rotate category buckets.
class WeightedSelection {
public:
    struct Entry { std::size_t index; float cumulative; bool* used; };

    template<class Shots, class Eligible>
    void append_category(Shots& shots, std::string_view category, Eligible eligible) {
        const auto before = entries_.size();
        for (std::size_t i = 0; i < shots.size(); ++i) {
            auto& shot = shots[i];
            if (shot.category != category || shot.selection_used || !eligible(shot)) continue;
            append(i, shot.selection_weight, shot.selection_used);
        }
        if (entries_.size() != before) return;
        for (std::size_t i = 0; i < shots.size(); ++i) {
            auto& shot = shots[i];
            if (shot.category != category || !eligible(shot)) continue;
            shot.selection_used = false;
            append(i, shot.selection_weight, shot.selection_used);
        }
    }

    std::optional<std::size_t> choose(float draw) {
        for (const auto& entry : entries_) {
            if (draw <= entry.cumulative) {
                *entry.used = true;
                return entry.index;
            }
        }
        return std::nullopt;
    }

    float total() const { return total_; }
    std::size_t size() const { return entries_.size(); }

private:
    void append(std::size_t index, float weight, bool& used) {
        total_ += weight;
        entries_.push_back({index, total_, &used});
    }
    std::vector<Entry> entries_;
    float total_ = 0;
};

} // namespace ghogx::camera
