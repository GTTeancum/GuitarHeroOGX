#include "render/gh1_crowd_regions.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using ghogx::render::Gh1CrowdRegions;
namespace ms = ghogx::milo_scene;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

ms::MeshObj triangle(const char* name) {
  ms::MeshObj m;
  m.name = name; m.decoded = true;
  m.verts.resize(3);
  m.verts[0].px = 0; m.verts[0].py = 0;
  m.verts[1].px = 10; m.verts[1].py = 0;
  m.verts[2].px = 0; m.verts[2].py = 10;
  m.indices = {0, 1, 2};
  return m;
}

int main() {
  auto mesh = triangle("crowd_limits00.mesh");
  CHECK(Gh1CrowdRegions::contains_local(mesh, {1, 1, 50}));
  CHECK(Gh1CrowdRegions::contains_local(mesh, {5, 5, 50}));
  CHECK(Gh1CrowdRegions::contains_local(mesh, {0, 0, 50}));
  CHECK(!Gh1CrowdRegions::contains_local(mesh, {6, 6, 50}));
  CHECK(!Gh1CrowdRegions::contains_local(mesh, {1, 1, 0}));
  CHECK(!Gh1CrowdRegions::contains_local(mesh, {1, 1, 100.8f}));
  CHECK(Gh1CrowdRegions::contains_local(mesh, {1, 1, 100.79f}));
  CHECK(!Gh1CrowdRegions::contains_local(mesh, {1, 1, 20}, 20));
  CHECK(!Gh1CrowdRegions::contains_local(mesh, {1, 1, std::numeric_limits<float>::quiet_NaN()}));
  std::swap(mesh.indices[1], mesh.indices[2]);
  CHECK(Gh1CrowdRegions::contains_local(mesh, {1, 1, 50})); // opposite branch/winding
  mesh.verts[1].px = 0;
  CHECK(!Gh1CrowdRegions::contains_xy(mesh, 0, 1)); // degenerate, no XY area
  mesh.indices[0] = 500;
  CHECK(!Gh1CrowdRegions::contains_xy(mesh, 0, 1)); // malformed input is safe
  // Basement region 5, triangle 17 and Crowd02 instance from the retail save.
  // Generic nearest arithmetic OR chopping every operation drops this member.
  auto precision = triangle("crowd_limits05.mesh");
  precision.verts[0].px = 1.055725988408085e-05f;
  precision.verts[0].py = 32.39999771118164f;
  precision.verts[1].px = 32.26202392578125f;
  precision.verts[1].py = 32.399993896484375f;
  precision.verts[2].px = 2.11145197681617e-05f;
  precision.verts[2].py = 64.79999542236328f;
  CHECK(Gh1CrowdRegions::contains_xy(precision, 21.431800842285156f, 39.172637939453125f));
  CHECK(!Gh1CrowdRegions::contains_xy(precision, 40.0f, 39.172637939453125f));
  CHECK(std::fabs(Gh1CrowdRegions::region_score(.5f, .5f, 100) - 75) < .001f);
  CHECK(std::fabs(Gh1CrowdRegions::region_score(.5f, 1.5f, 2) - 2) < .001f);

  ms::Scene scene;
  scene.meshes.push_back(triangle("crowd_limits00.mesh"));
  scene.meshes.push_back(triangle("crowd_limits01.mesh"));
  // Translation and nonuniform scale: world (12,23,10) -> local (1,1,5).
  auto& transformed = scene.meshes[1];
  transformed.local.rot[0][0] = 2;
  transformed.local.rot[1][1] = 3;
  transformed.local.rot[2][2] = 2;
  transformed.local.pos[0] = 10; transformed.local.pos[1] = 20;
  transformed.local.pos[2] = -7;
  transformed.world_stored = transformed.local;
  ms::MultiMeshObj crowd;
  crowd.name = "Crowd01.mm";
  crowd.instances.resize(4);
  crowd.instances[0].pos[0] = 1; crowd.instances[0].pos[1] = 1; crowd.instances[0].pos[2] = 10;
  crowd.instances[1].pos[0] = 2; crowd.instances[1].pos[1] = 2; crowd.instances[1].pos[2] = 10;
  crowd.instances[2].pos[0] = 12; crowd.instances[2].pos[1] = 23; crowd.instances[2].pos[2] = 10;
  crowd.instances[3].pos[0] = 14; crowd.instances[3].pos[1] = 26; crowd.instances[3].pos[2] = 10;
  scene.multi_meshes.push_back(crowd);
  ms::GroupObj owner;
  owner.name = "__gh1_runtime_multimeshes.grp";
  owner.children = {crowd.name};
  scene.groups.push_back(owner);
  Gh1CrowdRegions regions;
  regions.rebuild(scene);
  const auto* a = &scene.multi_meshes[0].instances[0];
  const auto* b = &scene.multi_meshes[0].instances[2];
  CHECK(regions.regions().size() == 2);
  CHECK(regions.regions()[0].members.size() == 2);
  CHECK(regions.regions()[1].members.size() == 2);
  CHECK(regions.regions()[0].center[0] == 1.5f);
  CHECK(std::fabs(regions.regions()[0].radius - std::sqrt(2.f)) < .001f);
  CHECK(regions.select(0)); CHECK(!regions.allows_flat(a)); CHECK(regions.allows_flat(b));
  CHECK(regions.active_flat_count() == 2); CHECK(regions.promoted_count() == 2);
  regions.set_sizes(0.5f, 0.5f);
  CHECK(regions.promoted_count() == 1); CHECK(regions.active_flat_count() == 1);
  const auto promoted = regions.promoted_worlds();
  CHECK(promoted.size() == 1); CHECK(promoted[0][12] == 1.0f);
  CHECK(promoted[0][13] == 1.0f); CHECK(promoted[0][14] == 0.0f);
  const auto replacements = regions.replacement_worlds();
  CHECK(replacements.size() == 2); // promoted + retained far card, once each
  CHECK(replacements[0] == promoted[0]);
  CHECK(replacements[1][12] == 14.0f && replacements[1][14] == -7.0f);
  CHECK(regions.owns(a) && regions.owns(b) && !regions.owns(nullptr));
  CHECK(!regions.allows_flat(a)); CHECK(!regions.allows_flat(b));
  CHECK(regions.allows_flat(&scene.multi_meshes[0].instances[3]));
  // Release crowd-quality contract: reducing the promoted/fullness fraction
  // must never restore a camera-selected near/mid member as a flat card.
  // This audits the retail population; presentation replaces its cards in 3D.
  regions.set_sizes(0.0f, 1.0f);
  CHECK(regions.promoted_count() == 0);
  for (const auto* selected_member : regions.regions()[0].ordered_members)
    CHECK(!regions.allows_flat(selected_member));
  CHECK(regions.allows_flat(&scene.multi_meshes[0].instances[2]));
  CHECK(regions.allows_flat(&scene.multi_meshes[0].instances[3]));
  regions.set_sizes(0.5f, 0.5f);
  CHECK(Gh1CrowdRegions::source_keep_count(5, 0.5f) == 2); // nearest-even cvt.w.s
  CHECK(Gh1CrowdRegions::source_keep_count(6, 0.5f) == 3);
  CHECK(Gh1CrowdRegions::source_keep_count(4, 0.25f) == 1);
  CHECK(Gh1CrowdRegions::source_keep_count(4, 0.0f) == 0);
  CHECK(Gh1CrowdRegions::source_keep_count(4, 1.0f) == 4);
  CHECK(regions.select(1)); CHECK(!regions.allows_flat(a)); CHECK(!regions.allows_flat(b));
  const auto transformed_promoted = regions.promoted_worlds();
  CHECK(transformed_promoted.size() == 1);
  CHECK(transformed_promoted[0][12] == 12.0f);
  CHECK(transformed_promoted[0][13] == 23.0f);
  CHECK(transformed_promoted[0][14] == -7.0f);
  CHECK(regions.allows_flat(&scene.multi_meshes[0].instances[1]));
  regions.set_sizes(1.0f, 1.0f);
  CHECK(regions.active_flat_count() == 2); CHECK(regions.promoted_count() == 2);
  CHECK(regions.allows_flat(nullptr));
  ms::Xfm unrelated;
  CHECK(regions.allows_flat(&unrelated));
  CHECK(!regions.select(99)); CHECK(regions.selected() == 1);
  CHECK(!regions.select(-1)); CHECK(regions.selected() == 1);
  const std::array<float, 16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const std::array<float, 16> projection{1,0,0,0, 0,1,0,0, 0,0,1,1, 0,0,0,0};
  CHECK(regions.select_auto(identity, projection)); CHECK(regions.selected() == 0);
  // Ground Z comes from first geometry vertex transformed to world, not
  // negated translation or just the object's origin.
  scene.meshes[1].verts[0].pz = 2.0f;
  regions.rebuild(scene);
  CHECK(regions.regions()[1].plane_z == -3.0f);
  scene.meshes[1].verts[0].pz = 0.0f;
  // Equal regions select the first on a strict positive score comparison.
  scene.meshes[1].local = scene.meshes[0].local;
  scene.meshes[1].world_stored = scene.meshes[0].world_stored;
  regions.rebuild(scene);
  CHECK(regions.select_auto(identity, projection)); CHECK(regions.selected() == 0);
  // Source only discovers consecutive authored indices.
  scene.meshes[1].name = "crowd_limits02.mesh";
  regions.rebuild(scene); CHECK(regions.regions().size() == 1);
  // A GH2 scene with similarly named helpers is not sufficient to opt in.
  scene.groups.clear(); regions.rebuild(scene);
  CHECK(regions.regions().empty()); CHECK(regions.allows_flat(a));
  CHECK(!regions.select_auto(identity, projection));
  // Retail discovers numbered archetypes, not directory serialization order,
  // and stops at the first gap. Use distinct origins to verify the 3D prefix.
  ms::Scene shuffled;
  shuffled.meshes.push_back(triangle("crowd_limits00.mesh"));
  shuffled.groups.push_back(owner);
  for (int number : {3, 1, 2}) {
    auto item = crowd;
    item.name = "Crowd0" + std::to_string(number) + ".mm";
    item.instances.resize(1);
    item.instances[0].pos[0] = static_cast<float>(number);
    shuffled.multi_meshes.push_back(item);
    shuffled.groups[0].children.push_back(item.name);
  }
  regions.rebuild(shuffled);
  CHECK(regions.regions()[0].ordered_members.size() == 3);
  for (int number = 1; number <= 3; ++number)
    CHECK(regions.regions()[0].ordered_members[number-1]->pos[0] == number);
  shuffled.multi_meshes.back().name = "Crowd04.mm"; // missing Crowd02
  regions.rebuild(shuffled);
  CHECK(regions.regions()[0].ordered_members.size() == 1);
  // The converted lighting section is also assembled into the world scene.
  // Only identical Arena-owned draws are deduplicated; transforms, geometry,
  // all native GH2 instances and non-crowd render objects remain untouched.
  ms::Scene primary;
  auto card = triangle("card.mesh");
  card.material = "crowd.mat";
  primary.meshes.push_back(card);
  crowd.mesh = card.name;
  primary.multi_meshes.push_back(crowd);
  primary.groups.push_back(owner);
  ms::Scene secondary = primary;
  CHECK(Gh1CrowdRegions::duplicate_drawables(primary, secondary).count(crowd.name));
  CHECK(secondary.multi_meshes[0].showing);
  secondary.multi_meshes[0].instances[0].pos[0] += 1;
  CHECK(Gh1CrowdRegions::duplicate_drawables(primary, secondary).empty());
  secondary = primary;
  secondary.meshes[0].verts[0].u += 0.5f;
  CHECK(Gh1CrowdRegions::duplicate_drawables(primary, secondary).empty());
  secondary = primary;
  secondary.meshes[0].material = "other.mat";
  CHECK(Gh1CrowdRegions::duplicate_drawables(primary, secondary).empty());
  secondary = primary;
  secondary.groups.clear();
  CHECK(Gh1CrowdRegions::duplicate_drawables(primary, secondary).empty());
  secondary = primary;
  primary.groups.clear();
  CHECK(Gh1CrowdRegions::duplicate_drawables(primary, secondary).empty());
  ms::Scene far_scene;
  auto far_card = triangle("card.mesh");
  for (auto& v : far_card.verts) v.pz = -20;
  far_scene.meshes.push_back(far_card);
  ms::MultiMeshObj far_crowd;
  far_crowd.name = "Crowd01.mm"; far_crowd.mesh = far_card.name;
  far_crowd.instances.resize(1); far_crowd.instances[0].pos[2] = 60;
  far_scene.multi_meshes.push_back(far_crowd);
  owner.children = {far_crowd.name}; far_scene.groups.push_back(owner);
  Gh1CrowdRegions far_regions; far_regions.rebuild(far_scene);
  CHECK(far_regions.replacement_worlds().size() == 1);
  CHECK(far_regions.replacement_worlds()[0][14] == 40);
  std::puts("GH1 crowd regions: membership, boundaries, affine scale, ownership, scoring, population, replacement grounding and lifecycle PASS");
}
