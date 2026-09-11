#include "render/gh1_crowd_regions.h"
#include "core/ee_float.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ghogx::render {
namespace {
// Full inverse: region transforms may contain nonuniform scale. Transposing
// the basis (a rigid-only inverse) changes the authored membership volumes.
bool local_point(const std::array<float, 16>& w, const float* p,
                 std::array<float, 3>& out) {
  const float a = w[0], b = w[1], c = w[2];
  const float d = w[4], e = w[5], f = w[6];
  const float g = w[8], h = w[9], i = w[10];
  const float det = a * (e*i-f*h) - b * (d*i-f*g) + c * (d*h-e*g);
  if (!std::isfinite(det) || det == 0) return false;
  const float x = p[0]-w[12], y = p[1]-w[13], z = p[2]-w[14];
  out = {(x*(e*i-f*h) + y*(f*g-d*i) + z*(d*h-e*g))/det,
         (x*(c*h-b*i) + y*(a*i-c*g) + z*(b*g-a*h))/det,
         (x*(b*f-c*e) + y*(c*d-a*f) + z*(a*e-b*d))/det};
  return true;
}
std::array<float, 4> transform(const std::array<float, 4>& p,
                             const std::array<float, 16>& m) {
  std::array<float, 4> out{};
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) out[c] += p[r] * m[r*4+c];
  return out;
}
}  // namespace

bool Gh1CrowdRegions::contains_xy(const milo_scene::MeshObj& mesh,
                                float x, float y) {
  using namespace ghogx::ee;
  // SLUS_212.24 0x1E3600..0x1E37AC. Preserve its two divisions/axis
  // branches, 1e-5 threshold, inclusive barycentric edges, either winding.
  for (size_t n = 0; n + 2 < mesh.indices.size(); n += 3) {
    const auto ia = mesh.indices[n], ib = mesh.indices[n+1], ic = mesh.indices[n+2];
    if (ia >= mesh.verts.size() || ib >= mesh.verts.size() || ic >= mesh.verts.size()) continue;
    const auto& a = mesh.verts[ia];
    const auto& b = mesh.verts[ib];
    const auto& c = mesh.verts[ic];
    const float cx = ee_sub(c.px,a.px), cy = ee_sub(c.py,a.py);
    const float bx = ee_sub(b.px,a.px), by = ee_sub(b.py,a.py);
    const float px = ee_sub(x,a.px), py = ee_sub(y,a.py);
    float u, v;
    if (std::fabs(cx) > 1.0e-5f) {
      const float slope = ee_div(cy,cx);
      u = ee_div(ee_sub(py,ee_mul(slope,px)),ee_sub(by,ee_mul(slope,bx)));
      if (!(u >= 0 && u <= 1)) continue;
      v = ee_div(ee_sub(px,ee_mul(u,bx)),cx);
    } else if (std::fabs(bx) > 1.0e-5f) {
      const float slope = ee_div(by,bx);
      u = ee_div(ee_sub(py,ee_mul(slope,px)),ee_sub(cy,ee_mul(slope,cx)));
      if (!(u >= 0 && u <= 1)) continue;
      v = ee_div(ee_sub(px,ee_mul(u,cx)),bx);
    } else {
      continue;
    }
    if (v >= 0 && ee_add(u,v) <= 1) return true;
  }
  return false;
}

bool Gh1CrowdRegions::contains_local(const milo_scene::MeshObj& mesh,
                                    const std::array<float, 3>& p,
                                    float flat_height) {
  // 0x170F98..0x170FD4; default from system config/arena.dtb, not mesh AABB.
  return p[2] > 0 && p[2] < flat_height && contains_xy(mesh, p[0], p[1]);
}

void Gh1CrowdRegions::rebuild(const milo_scene::Scene& scene) {
  regions_.clear();
  instances_.clear();
  instance_runs_.clear();
  active_flat_.clear();
  card_ground_z_.clear();
  selected_ = -1;
  // Converter records the recovered Arena::Crowd ownership. Never apply this
  // policy to arbitrary native GH2 MultiMeshes or WorldCrowd instances.
  const auto owner = std::find_if(scene.groups.begin(), scene.groups.end(),
      [](const auto& g) { return g.name == "__gh1_runtime_multimeshes.grp"; });
  if (owner == scene.groups.end()) return;
  const bool audit = std::getenv("GHOGX_DEBUG_VENUE_FILTERS") != nullptr;
  if (audit) {
    for (const auto& group : scene.groups) {
      if (group.name.find("__gh1_runtime_multimeshes") == std::string::npos)
        continue;
      std::fprintf(stderr,
          "[crowd_region] scene=%s owner=%s children=%zu used=%d\n",
          scene.dir_name.c_str(), group.name.c_str(), group.children.size(),
          &group == &*owner);
    }
  }
  // Arena::Crowd initialization 0x171A14/0x171A50..0x171AB8 resolves
  // arena::Crowd%02d.mm starting at 1, stopping at the first absent object.
  // Directory serialization order is different, and changes the 3D prefix.
  for (int archetype = 1;; ++archetype) {
    char name[64];
    std::snprintf(name, sizeof(name), "Crowd%02d.mm", archetype);
    const auto found = std::find_if(scene.multi_meshes.begin(), scene.multi_meshes.end(),
        [&](const auto& multi) { return multi.name == name; });
    if (found == scene.multi_meshes.end()) break;
    const auto& multi = *found;
    const bool owned = std::find(owner->children.begin(), owner->children.end(),
                                 multi.name) != owner->children.end();
    if (audit) {
      std::fprintf(stderr,
          "[crowd_region] multimesh=%s template=%s instances=%zu owned=%d\n",
          multi.name.c_str(), multi.mesh.c_str(), multi.instances.size(), owned);
    }
    if (!owned) continue;
    auto& run = instance_runs_.emplace_back();
    run.reserve(multi.instances.size());
    for (const auto& instance : multi.instances) {
      instances_.insert(&instance);
      run.push_back(&instance);
      const auto mesh = std::find_if(scene.meshes.begin(), scene.meshes.end(),
          [&](const auto& m) { return m.name == multi.mesh; });
      if (mesh != scene.meshes.end()) {
        const auto* geometry = &*mesh;
        if (!mesh->geometry_owner.empty() && mesh->geometry_owner != mesh->name) {
          const auto shared = std::find_if(scene.meshes.begin(), scene.meshes.end(),
              [&](const auto& m) { return m.name == mesh->geometry_owner; });
          if (shared != scene.meshes.end()) geometry = &*shared;
        }
        bool first_vertex = true;
        float bottom = instance.pos[2];
        for (const auto& v : geometry->verts) {
          const float z = instance.pos[2] + v.px * instance.rot[0][2] +
                          v.py * instance.rot[1][2] + v.pz * instance.rot[2][2];
          bottom = first_vertex ? z : std::min(bottom, z);
          first_vertex = false;
        }
        card_ground_z_[&instance] = bottom;
      }
    }
  }
  // 0x171F58..0x1720AC: consecutive names from 00, stop at first missing mesh.
  const auto find_mesh = [&](const std::string& name) -> const milo_scene::MeshObj* {
    const auto it = std::find_if(scene.meshes.begin(), scene.meshes.end(),
                                [&](const auto& m) { return m.name == name; });
    return it == scene.meshes.end() ? nullptr : &*it;
  };
  for (int index = 0;; ++index) {
    char name[64];
    std::snprintf(name, sizeof(name), "crowd_limits%02d.mesh", index);
    const auto* limit = find_mesh(name);
    if (!limit) break;
    const milo_scene::MeshObj* geometry = limit;
    if (!limit->geometry_owner.empty() && limit->geometry_owner != limit->name) {
      if (const auto* shared = find_mesh(limit->geometry_owner)) geometry = shared;
    }
    const auto world = scene.world_matrix(*limit);
    Region region;
    // 0x171338..0x171374 overwrites the earlier inverse-translation scratch
    // with the FIRST GEOMETRY VERTEX transformed to world space. Its Z is
    // stored at region+0x40 (0x1713BC), then used by SetRegion (0x172BEC).
    // It is not the negated transform translation used during membership.
    if (!geometry->verts.empty()) {
      const auto& v = geometry->verts.front();
      region.plane_z = v.px*world[2] + v.py*world[6] + v.pz*world[10] + world[14];
    }
    std::array<float, 3> lo{}, hi{};
    bool first = true;
    for (const auto& run : instance_runs_) {
      for (const auto* instance : run) {
        std::array<float, 3> local{};
        if (!local_point(world, instance->pos, local) ||
            !contains_local(*geometry, local))
          continue;
        region.members.insert(instance);
        region.ordered_members.push_back(instance);
        for (int k = 0; k < 3; ++k) {
          lo[k] = first ? instance->pos[k]
                        : std::min(lo[k], instance->pos[k]);
          hi[k] = first ? instance->pos[k]
                        : std::max(hi[k], instance->pos[k]);
        }
        first = false;
      }
    }
    // 0x1712A8..0x171424: bounds of accepted WORLD instance origins, center
    // at midpoint, radius = length(max-min), not a conventional half diagonal.
    float length_sq = 0;
    for (int k = 0; k < 3; ++k) {
      region.center[k] = (lo[k]+hi[k])*0.5f;
      length_sq += (hi[k]-lo[k])*(hi[k]-lo[k]);
    }
    region.radius = std::sqrt(length_sq);
    regions_.push_back(std::move(region));
  }
  rebuild_active_flat();
}

bool Gh1CrowdRegions::select(int index) {
  if (index < 0 || static_cast<size_t>(index) >= regions_.size()) return false;
  selected_ = index;
  rebuild_active_flat();
  return true;
}

size_t Gh1CrowdRegions::source_keep_count(size_t count, float fraction) {
  if (count == 0 || !std::isfinite(fraction) || fraction <= 0.0f) return 0;
  if (fraction >= 1.0f) return count;
  // Arena::Crowd::SetSizes, SLUS_212.24 0x172F84..0x1730E4: multiply the
  // available count by the requested fraction, then cvt.w.s. The caller
  // separately applies the flat-list truncation direction.
  const long rounded = std::lrint(static_cast<float>(count) * fraction);
  return static_cast<size_t>(
      std::clamp<long>(rounded, 0, static_cast<long>(count)));
}

void Gh1CrowdRegions::set_sizes(float promoted_fraction,
                                float flat_fraction) {
  promoted_fraction_ = std::isfinite(promoted_fraction)
                           ? std::clamp(promoted_fraction, 0.0f, 1.0f)
                           : 0.0f;
  flat_fraction_ = std::isfinite(flat_fraction)
                       ? std::clamp(flat_fraction, 0.0f, 1.0f)
                       : 0.0f;
  rebuild_active_flat();
}

size_t Gh1CrowdRegions::promoted_count() const {
  if (selected_ < 0 || static_cast<size_t>(selected_) >= regions_.size()) {
    return 0;
  }
  return source_keep_count(regions_[selected_].members.size(),
                           promoted_fraction_);
}

std::vector<std::array<float, 16>> Gh1CrowdRegions::promoted_worlds() const {
  std::vector<std::array<float, 16>> worlds;
  if (selected_ < 0 || static_cast<size_t>(selected_) >= regions_.size()) {
    return worlds;
  }
  const auto& region = regions_[selected_];
  const size_t count = std::min(promoted_count(), region.ordered_members.size());
  worlds.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const auto& xfm = *region.ordered_members[i];
    worlds.push_back({
        xfm.rot[0][0], xfm.rot[0][1], xfm.rot[0][2], 0.0f,
        xfm.rot[1][0], xfm.rot[1][1], xfm.rot[1][2], 0.0f,
        xfm.rot[2][0], xfm.rot[2][1], xfm.rot[2][2], 0.0f,
        xfm.pos[0], xfm.pos[1], region.plane_z, 1.0f});
  }
  return worlds;
}

std::vector<std::array<float, 16>> Gh1CrowdRegions::replacement_worlds() const {
  auto worlds = promoted_worlds();
  for (const auto& run : instance_runs_) {
    for (const auto* instance : run) {
      if (!active_flat_.count(instance)) continue;
      float ground = instance->pos[2];
      const auto bottom = card_ground_z_.find(instance);
      if (bottom != card_ground_z_.end()) ground = bottom->second;
      for (const auto& region : regions_) {
        if (region.members.count(instance)) { ground = region.plane_z; break; }
      }
      const auto& x = *instance;
      worlds.push_back({x.rot[0][0], x.rot[0][1], x.rot[0][2], 0,
                        x.rot[1][0], x.rot[1][1], x.rot[1][2], 0,
                        x.rot[2][0], x.rot[2][1], x.rot[2][2], 0,
                        x.pos[0], x.pos[1], ground, 1});
    }
  }
  return worlds;
}

void Gh1CrowdRegions::rebuild_active_flat() {
  active_flat_.clear();
  const Region* selected =
      selected_ >= 0 && static_cast<size_t>(selected_) < regions_.size()
          ? &regions_[selected_]
          : nullptr;
  for (const auto& run : instance_runs_) {
    std::vector<const milo_scene::Xfm*> available;
    available.reserve(run.size());
    for (const auto* instance : run) {
      if (!selected || selected->members.count(instance) == 0) {
        available.push_back(instance);
      }
    }
    const size_t keep = source_keep_count(available.size(), flat_fraction_);
    // SetSizes 0x17308C..0x1730E4 transfers the unwanted FRONT nodes to the
    // held list, leaving the suffix in the live MultiMesh. Keeping a prefix
    // gives the same counts but the wrong visible population. This suffix
    // matches all 36 visible cards in the retained retail Basement snapshot.
    active_flat_.insert(available.end() - keep, available.end());
  }
}

float Gh1CrowdRegions::region_score(float u, float v, float projected_radius) {
  const float x = u-0.5f, y = v-0.5f;
  const float distance = std::sqrt(x*x+y*y);
  // 0x172974..0x1729DC. Ordered comparisons preserve source NaN behavior.
  const float radius = projected_radius < 15.0f ? projected_radius : 15.0f;
  return radius / (distance < 0.2f ? 0.2f : distance);
}

bool Gh1CrowdRegions::select_auto(const std::array<float, 16>& view,
                                const std::array<float, 16>& projection) {
  if (regions_.empty()) return false;
  int best = static_cast<int>(regions_.size())-1;
  float best_score = 0;
  for (size_t i = 0; i < regions_.size(); ++i) {
    const auto& r = regions_[i];
    const auto camera = transform({r.center[0], r.center[1], r.center[2], 1}, view);
    const auto clip = transform(camera, projection);
    // RndCam::Project 0x1B1100: zero depth skips the divide, no behind-camera
    // rejection. Full viewport coordinates; mirroring either axis leaves
    // distance to (0.5,0.5) unchanged.
    const float divisor = clip[3] == 0 ? 1 : clip[3];
    const float u = (clip[0]/divisor+1)*0.5f;
    const float v = (clip[1]/divisor+1)*0.5f;
    const float radius = std::fabs(r.radius*projection[0]/camera[2]);
    const float score = region_score(u, v, radius);
    if (score > best_score) { best_score = score; best = static_cast<int>(i); }
  }
  selected_ = best;
  rebuild_active_flat();
  return true;
}

bool Gh1CrowdRegions::allows_flat(const milo_scene::Xfm* instance) const {
  // 0x172A88 restores the previous region's held nodes to the MultiMesh.
  // 0x172B04 moves each selected node OUT of that MultiMesh into archetype+14.
  // 0x172BA8 assigns the corresponding transforms to the 3-D character pool.
  // Release invariant: a member of the camera-selected region is near/mid
  // crowd and may NEVER fall back to its legacy flat card.  SetSizes may
  // reduce the number of live 3-D actors for authored crowd fullness, but the
  // remaining selected members stay absent rather than silently degrading to
  // 2-D.  Only non-selected (distant) members can enter active_flat_.
  // This list is a flat-card exclusion/promotion list, NOT a visibility list.
  return !instances_.count(instance) || active_flat_.count(instance) != 0;
}

std::unordered_set<std::string> Gh1CrowdRegions::duplicate_drawables(
    const milo_scene::Scene& owner, const milo_scene::Scene& secondary) {
  const auto owned_names = [](const milo_scene::Scene& scene) {
    std::unordered_set<std::string> names;
    for (const auto& group : scene.groups) {
      if (group.name == "__gh1_runtime_multimeshes.grp")
        names.insert(group.children.begin(), group.children.end());
    }
    return names;
  };
  const auto primary_names = owned_names(owner);
  const auto secondary_names = owned_names(secondary);
  const auto equal_xfm = [](const auto& a, const auto& b) {
    for (int row = 0; row < 3; ++row) {
      if (a.pos[row] != b.pos[row]) return false;
      for (int col = 0; col < 3; ++col)
        if (a.rot[row][col] != b.rot[row][col]) return false;
    }
    return true;
  };
  const auto mesh_for = [](const auto& scene, const auto& name) {
    return std::find_if(scene.meshes.begin(), scene.meshes.end(),
                        [&](const auto& mesh) { return mesh.name == name; });
  };
  const auto equal_vertex = [](const auto& a, const auto& b) {
    return a.px == b.px && a.py == b.py && a.pz == b.pz &&
           a.nx == b.nx && a.ny == b.ny && a.nz == b.nz &&
           a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a &&
           a.u == b.u && a.v == b.v &&
           std::equal(std::begin(a.w), std::end(a.w), std::begin(b.w));
  };
  std::unordered_set<std::string> duplicates;
  for (const auto& multi : secondary.multi_meshes) {
    if (!secondary_names.count(multi.name) || !primary_names.count(multi.name))
      continue;
    const auto primary = std::find_if(owner.multi_meshes.begin(),
        owner.multi_meshes.end(), [&](const auto& item) {
          return item.name == multi.name && item.mesh == multi.mesh;
        });
    if (primary == owner.multi_meshes.end() ||
        primary->instances.size() != multi.instances.size() ||
        !std::equal(primary->instances.begin(), primary->instances.end(),
                    multi.instances.begin(), equal_xfm)) continue;
    const auto a = mesh_for(owner, primary->mesh);
    const auto b = mesh_for(secondary, multi.mesh);
    if (a == owner.meshes.end() || b == secondary.meshes.end() ||
        !a->decoded || !b->decoded || a->material != b->material ||
        a->geometry_owner != b->geometry_owner || a->verts.empty() ||
        a->verts.size() != b->verts.size() || a->indices != b->indices ||
        !std::equal(a->verts.begin(), a->verts.end(), b->verts.begin(),
                    equal_vertex)) continue;
    duplicates.insert(multi.name);
  }
  return duplicates;
}
}  // namespace ghogx::render
