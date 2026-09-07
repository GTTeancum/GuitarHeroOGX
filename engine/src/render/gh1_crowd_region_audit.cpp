// Offline audit using the SAME region implementation as the renderer.
// Reads one converted section, never creates a window or modifies its input.
#include "render/gh1_crowd_regions.h"
#include "milo.h"
#include "milo_object.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>

namespace ms = ghogx::milo_scene;
ms::Xfm xfm(const std::array<float, 12>& value) {
  ms::Xfm out;
  for (int row = 0; row < 3; ++row) {
    out.pos[row] = value[9 + row];
    for (int col = 0; col < 3; ++col) out.rot[row][col] = value[row*3 + col];
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc != 2 && argc != 5) return 2;
  try {
    const auto bytes = gh::milo::read_file(argv[1]);
    const auto directory = gh::milo::parse_directory(
        gh::milo::inflate_payload(bytes, gh::milo::parse_header(bytes)));
    ms::Scene scene;
    for (const auto& entry : directory.entries) {
      if (entry.type == "MultiMesh") {
        const auto raw = gh::milo_object::parse_multi_mesh1(entry.body_bytes);
        ms::MultiMeshObj multi;
        multi.name = entry.name; multi.mesh = raw.mesh; multi.decoded = true;
        for (const auto& transform : raw.transforms)
          multi.instances.push_back(xfm(transform));
        scene.multi_meshes.push_back(std::move(multi));
      } else if (entry.type == "Group" &&
                 entry.name == "__gh1_runtime_multimeshes.grp") {
        const auto raw = gh::milo_object::parse_group12(entry.body_bytes);
        ms::GroupObj group;
        group.name = entry.name; group.children = raw.objects;
        scene.groups.push_back(std::move(group));
      } else if (entry.type == "Mesh" &&
                 entry.name.rfind("crowd_limits", 0) == 0) {
        const auto raw = gh::milo_object::parse_mesh28(entry.body_bytes);
        if (!raw.transformable.parent.empty())
          throw std::runtime_error("Audit needs complete parent graph: " + entry.name);
        ms::MeshObj mesh;
        mesh.name = entry.name; mesh.decoded = true;
        mesh.geometry_owner = raw.geometry_owner;
        mesh.local = xfm(raw.transformable.local);
        mesh.world_stored = xfm(raw.transformable.world);
        for (const auto& vertex : raw.vertices) {
          ms::Vertex v{};
          v.px = vertex.position[0]; v.py = vertex.position[1]; v.pz = vertex.position[2];
          mesh.verts.push_back(v);
        }
        for (const auto& face : raw.faces)
          mesh.indices.insert(mesh.indices.end(), face.begin(), face.end());
        scene.meshes.push_back(std::move(mesh));
      }
    }
    ghogx::render::Gh1CrowdRegions regions;
    regions.rebuild(scene);
    if (regions.regions().empty()) throw std::runtime_error("No owned crowd regions");
    std::unordered_map<const ms::Xfm*, std::string> owners;
    for (const auto& multi : scene.multi_meshes) {
      for (const auto& instance : multi.instances) {
        owners[&instance] = multi.name;
        std::printf("INSTANCE\t%s\t%.9g\t%.9g\t%.9g\n", multi.name.c_str(),
                    instance.pos[0], instance.pos[1], instance.pos[2]);
      }
    }
    size_t index = 0;
    for (const auto& region : regions.regions()) {
      std::printf("REGION\t%zu\t%zu\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\n",
          index, region.members.size(), region.center[0], region.center[1],
          region.center[2], region.radius, region.plane_z);
      for (const auto* instance : region.ordered_members)
        std::printf("MEMBER\t%zu\t%s\t%.9g\t%.9g\t%.9g\n", index,
            owners.at(instance).c_str(), instance->pos[0], instance->pos[1], instance->pos[2]);
      ++index;
    }
    if (argc == 5) {
      if (!regions.select(std::atoi(argv[2])))
        throw std::runtime_error("Invalid saved selected region");
      regions.set_sizes(std::strtof(argv[3], nullptr), std::strtof(argv[4], nullptr));
      std::printf("LIVE\t%zu\t%zu\n", regions.active_flat_count(), regions.promoted_count());
      for (const auto& multi : scene.multi_meshes)
        for (const auto& instance : multi.instances)
          if (regions.allows_flat(&instance))
            std::printf("ACTIVE\t%s\t%.9g\t%.9g\t%.9g\n", multi.name.c_str(),
                        instance.pos[0], instance.pos[1], instance.pos[2]);
    }
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
