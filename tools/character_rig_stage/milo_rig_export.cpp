#include "milo.h"
#include "milo_object.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string json_string(const std::string& value) {
    std::string out = "\"";
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                static const char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(ch >> 4) & 0xf];
                out += hex[ch & 0xf];
            } else {
                out += static_cast<char>(ch);
            }
        }
    }
    out += '"';
    return out;
}

template <size_t N>
void write_floats(std::ostream& out, const std::array<float, N>& values) {
    out << '[';
    for (size_t i = 0; i < N; ++i) {
        if (i) out << ',';
        if (std::isfinite(values[i])) out << values[i];
        else out << "null";
    }
    out << ']';
}

struct NodeRow {
    std::string name;
    std::string parent;
    std::array<float, 12> local{};
    std::string kind;
};

struct MeshRow {
    std::string name;
    gh::milo_object::Mesh28 mesh;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: milo_rig_export <input.milo_ps2> <output.rig.json>\n";
        return 2;
    }
    try {
        const auto bytes = gh::milo::read_file(argv[1]);
        const auto header = gh::milo::parse_header(bytes);
        const auto payload = gh::milo::inflate_payload(bytes, header);
        const auto directory = gh::milo::parse_directory(payload);
        std::vector<NodeRow> nodes;
        std::vector<MeshRow> meshes;
        for (const auto& entry : directory.entries) {
            if (entry.type == "Trans") {
                const auto trans = gh::milo_object::parse_trans9(entry.body_bytes);
                nodes.push_back({entry.name, trans.parent, trans.local, "Trans"});
            } else if (entry.type == "Mesh") {
                auto mesh = gh::milo_object::parse_mesh28(
                    entry.body_bytes, static_cast<uint32_t>(directory.dir_version));
                nodes.push_back({entry.name, mesh.transformable.parent,
                                 mesh.transformable.local, "Mesh"});
                meshes.push_back({entry.name, std::move(mesh)});
            }
        }

        std::ofstream out(argv[2], std::ios::binary);
        if (!out) throw std::runtime_error("cannot create output");
        out << std::setprecision(9);
        out << "{\n  \"schema\":\"ghogx.milo-rig-stage.v1\","
            << "\n  \"source\":" << json_string(argv[1])
            << ",\n  \"directory_revision\":" << directory.dir_version
            << ",\n  \"nodes\":[\n";
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& node = nodes[i];
            out << "    {\"name\":" << json_string(node.name)
                << ",\"parent\":" << json_string(node.parent)
                << ",\"kind\":" << json_string(node.kind)
                << ",\"local\":";
            write_floats(out, node.local);
            out << '}' << (i + 1 == nodes.size() ? "\n" : ",\n");
        }
        out << "  ],\n  \"meshes\":[\n";
        size_t skinned_meshes = 0;
        size_t weighted_vertices = 0;
        for (size_t i = 0; i < meshes.size(); ++i) {
            const auto& row = meshes[i];
            const auto& mesh = row.mesh;
            if (mesh.has_bones) ++skinned_meshes;
            out << "    {\"name\":" << json_string(row.name)
                << ",\"vertex_count\":" << mesh.vertices.size()
                << ",\"has_bones\":" << (mesh.has_bones ? "true" : "false")
                << ",\"bone_slots\":[";
            for (size_t slot = 0; slot < mesh.bone_slots.size(); ++slot) {
                if (slot) out << ',';
                out << "{\"name\":" << json_string(mesh.bone_slots[slot].bone)
                    << ",\"offset\":";
                write_floats(out, mesh.bone_slots[slot].offset);
                out << '}';
            }
            out << "],\"local\":";
            write_floats(out, mesh.transformable.local);
            out << ",\"world\":";
            write_floats(out, mesh.transformable.world);
            out << ",\"material\":" << json_string(mesh.material);
            out << ",\"positions\":[";
            for (size_t v = 0; v < mesh.vertices.size(); ++v) {
                if (v) out << ',';
                write_floats(out, mesh.vertices[v].position);
            }
            out << "],\"faces\":[";
            for (size_t f = 0; f < mesh.faces.size(); ++f) {
                if (f) out << ',';
                out << '[' << mesh.faces[f][0] << ',' << mesh.faces[f][1]
                    << ',' << mesh.faces[f][2] << ']';
            }
            out << "],\"weights\":[";
            for (size_t vertex_index = 0; vertex_index < mesh.vertices.size();
                 ++vertex_index) {
                if (vertex_index) out << ',';
                std::array<float, 4> weights{};
                for (size_t slot = 0; slot < weights.size(); ++slot)
                    // GH1/GH2 skinned four-float payload stays signed. Only
                    // unskinned vertex colors take the Hmx::Color32 path.
                    weights[slot] = mesh.vertices[vertex_index].color_or_weights[slot];
                if (mesh.has_bones &&
                    (weights[0] || weights[1] || weights[2] || weights[3]))
                    ++weighted_vertices;
                write_floats(out, weights);
            }
            out << "]}" << (i + 1 == meshes.size() ? "\n" : ",\n");
        }
        out << "  ]\n}\n";
        if (!out) throw std::runtime_error("failed while writing output");
        std::cout << "GHOGX_MILO_RIG_EXPORT_OK nodes=" << nodes.size()
                  << " meshes=" << meshes.size()
                  << " skinned_meshes=" << skinned_meshes
                  << " weighted_vertices=" << weighted_vertices << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "milo_rig_export: " << error.what() << '\n';
        return 1;
    }
}
