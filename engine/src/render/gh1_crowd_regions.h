#pragma once

#include "milo_scene/milo_scene.h"
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ghogx::render {

// GH1 Crowd, not GH2 WorldCrowd. Pointers refer to the owning Scene; rebuild
// after replacing that Scene. Selection never modifies authored transforms.
class Gh1CrowdRegions {
 public:
  struct Region {
    std::unordered_set<const milo_scene::Xfm*> members;
    std::vector<const milo_scene::Xfm*> ordered_members;
    std::array<float, 3> center{};
    float radius = 0;
    float plane_z = 0;
  };
  void rebuild(const milo_scene::Scene& scene);
  bool select(int index);
  bool select_auto(const std::array<float, 16>& view,
                   const std::array<float, 16>& projection);
  void set_sizes(float promoted_fraction, float flat_fraction);
  bool allows_flat(const milo_scene::Xfm* instance) const;
  const std::vector<Region>& regions() const { return regions_; }
  int selected() const { return selected_; }
  size_t instance_count() const { return instances_.size(); }
  size_t active_flat_count() const { return active_flat_.size(); }
  size_t promoted_count() const;
  std::vector<std::array<float, 16>> promoted_worlds() const;
  float promoted_fraction() const { return promoted_fraction_; }
  float flat_fraction() const { return flat_fraction_; }

  static bool contains_xy(const milo_scene::MeshObj& mesh, float x, float y);
  static bool contains_local(const milo_scene::MeshObj& mesh,
                             const std::array<float, 3>& p,
                             float flat_height = 100.8f);
  static float region_score(float u, float v, float projected_radius);
  static size_t source_keep_count(size_t count, float fraction);
  // Converted section graphs can be resident in both render passes. Retail
  // Arena owns one Crowd; do not submit a second, unselected copy of it.
  static std::unordered_set<std::string> duplicate_drawables(
      const milo_scene::Scene& owner, const milo_scene::Scene& secondary);

 private:
  void rebuild_active_flat();
  std::vector<Region> regions_;
  std::unordered_set<const milo_scene::Xfm*> instances_;
  std::vector<std::vector<const milo_scene::Xfm*>> instance_runs_;
  std::unordered_set<const milo_scene::Xfm*> active_flat_;
  float promoted_fraction_ = 1.0f;
  float flat_fraction_ = 1.0f;
  int selected_ = -1;
};

}  // namespace ghogx::render
