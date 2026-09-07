// milo_tool - CLI for Harmonix .milo container inspection.
//
// Usage:
//   milo_tool info    <file>             header only
//   milo_tool list    <file>             header + decompress + list (type,name,size)
//   milo_tool extract <file> --out <dir> decompress + dump each entry's raw bytes
//   milo_tool verify  <file>             byte-exact outer-container round trip

#include "milo.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  milo_tool info    <file>\n"
        "  milo_tool list    <file>\n"
        "  milo_tool strings <file> <entry>  bounded printable runs, not schema decoding\n"
        "  milo_tool extract-entry <file> <entry> --out <file>\n"
        "  milo_tool extract <file> --out <dir>\n"
        "  milo_tool verify  <file>\n");
    std::exit(2);
}

int main(int argc, char** argv) {
    if (argc < 3) usage();
    std::string sub = argv[1];
    std::string path = argv[2];
    try {
        auto bytes = gh::milo::read_file(path);
        auto h = gh::milo::parse_header(bytes);

        if (sub == "info") {
            std::printf("file              : %s\n", path.c_str());
            std::printf("size              : %zu bytes\n", bytes.size());
            std::printf("structure         : 0x%08X  (%s)\n",
                        static_cast<uint32_t>(h.structure),
                        gh::milo::block_structure_name(h.structure));
            std::printf("first_block_off   : 0x%X (%u)\n",
                        h.first_block_offset, h.first_block_offset);
            std::printf("block_count       : %u\n", h.block_count);
            std::printf("max_uncompressed  : %u bytes\n", h.max_block_uncompressed_size);
            for (uint32_t i = 0; i < h.block_count; ++i) {
                std::printf("  block[%u] size = %u\n", i, h.block_sizes[i]);
            }
            return 0;
        }

        if (sub == "verify") {
            const auto container = gh::milo::parse_container(bytes);
            const auto serialized = gh::milo::serialize_container(container);
            if (serialized != bytes) {
                std::fprintf(stderr,
                             "outer round trip differs: source=%zu output=%zu\n",
                             bytes.size(), serialized.size());
                return 1;
            }
            const auto payload = gh::milo::container_payload(container);
            std::printf("byte-exact outer round trip: %zu source bytes, "
                        "%zu payload bytes, %zu blocks\n",
                        bytes.size(), payload.size(), container.blocks.size());
            return 0;
        }

        auto payload = gh::milo::inflate_payload(bytes, h);
        if (sub == "extract-entry") {
            if (argc != 6 || std::strcmp(argv[4], "--out") != 0) usage();
            const fs::path output(argv[5]);
            if (fs::exists(output)) {
                std::fprintf(stderr, "refusing to overwrite: %s\n", argv[5]);
                return 1;
            }
            const auto d = gh::milo::parse_directory(payload);
            for (const auto& e : d.entries) {
                if (e.name != argv[3]) continue;
                if (!output.parent_path().empty()) fs::create_directories(output.parent_path());
                std::ofstream stream(output, std::ios::binary);
                if (!stream) throw std::runtime_error("cannot create entry output");
                stream.write(reinterpret_cast<const char*>(e.body_bytes.data()),
                             static_cast<std::streamsize>(e.body_bytes.size()));
                stream.close();
                if (!stream) throw std::runtime_error("entry output write failed");
                std::printf("extracted %s: %zu bytes\n", e.name.c_str(), e.body_bytes.size());
                return 0;
            }
            std::fprintf(stderr, "entry not found: %s\n", argv[3]);
            return 1;
        }
        if (sub == "strings") {
            if (argc != 4) usage();
            const auto d = gh::milo::parse_directory(payload);
            for (const auto& e : d.entries) {
                if (e.name != argv[3]) continue;
                std::printf("%s %s bytes=%zu (printable runs; not decoded fields)\n",
                            e.type.c_str(), e.name.c_str(), e.body_bytes.size());
                size_t emitted = 0;
                for (size_t i = 0; i < e.body_bytes.size();) {
                    const size_t start = i;
                    while (i < e.body_bytes.size() && e.body_bytes[i] >= 32 &&
                           e.body_bytes[i] <= 126) ++i;
                    const size_t count = i - start;
                    if (count >= 4) {
                        if (emitted++ == 64) {
                            std::printf("printable-run limit reached\n");
                            break;
                        }
                        std::printf("0x%zx %.*s%s\n", start,
                                    static_cast<int>(count < 160 ? count : 160),
                                    reinterpret_cast<const char*>(e.body_bytes.data() + start),
                                    count > 160 ? "..." : "");
                    }
                    if (i < e.body_bytes.size()) ++i;
                }
                return 0;
            }
            std::fprintf(stderr, "entry not found: %s\n", argv[3]);
            return 1;
        }
        if (sub == "list") {
            std::printf("decompressed payload: %zu bytes\n", payload.size());
            auto d = gh::milo::parse_directory(payload);
            std::printf("dir_version : %d\n", d.dir_version);
            std::printf("dir_type    : %s\n", d.dir_type.c_str());
            std::printf("dir_name    : %s\n", d.dir_name.c_str());
            std::printf("entries     : %zu\n", d.entries.size());
            std::printf("boundaries  : %s\n",
                        d.boundaries_exact ? "exact" : "fallback");

            std::map<std::string, size_t> by_type;
            size_t total_body_bytes = 0;
            for (const auto& e : d.entries) {
                by_type[e.type]++;
                total_body_bytes += e.size;
            }
            std::printf("\nType summary:\n");
            for (auto& [t, n] : by_type) {
                std::printf("  %-22s  %zu\n", t.c_str(), n);
            }
            std::printf("\nEntries:\n");
            for (const auto& e : d.entries) {
                std::printf("  %-22s  size=%-8llu body=%-8zu  %s\n",
                            e.type.c_str(),
                            (unsigned long long)e.size,
                            e.body_bytes.size(), e.name.c_str());
            }
            std::printf("\ntotal body bytes (sum of entry sizes): %zu\n", total_body_bytes);
            return 0;
        }
        if (sub == "extract") {
            std::string outdir;
            for (int i = 3; i < argc; ++i) {
                if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) { outdir = argv[++i]; continue; }
                std::fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2;
            }
            if (outdir.empty()) { std::fprintf(stderr, "--out required\n"); return 2; }
            auto d = gh::milo::parse_directory(payload);
            fs::create_directories(outdir);
            // Also write the full decompressed payload for reference / diffing.
            {
                std::ofstream o(fs::path(outdir) / "_payload.bin", std::ios::binary);
                o.write(reinterpret_cast<const char*>(payload.data()),
                        static_cast<std::streamsize>(payload.size()));
            }
            if (d.dir_entry_offset + d.dir_entry_size <= payload.size()) {
                std::ofstream o(fs::path(outdir) / (d.dir_type + "__" +
                                                    d.dir_name + ".root"),
                                std::ios::binary);
                o.write(reinterpret_cast<const char*>(payload.data() +
                                                      d.dir_entry_offset),
                        static_cast<std::streamsize>(d.dir_entry_size));
            }
            int ok = 0;
            for (const auto& e : d.entries) {
                if (e.body_bytes.empty() && e.size != 0) {
                    std::fprintf(stderr,
                                 "unresolved body boundary: %s %s (%llu bytes)\n",
                                 e.type.c_str(), e.name.c_str(),
                                 static_cast<unsigned long long>(e.size));
                    continue;
                }
                std::string safe = e.name;
                for (auto& c : safe) { if (c == '/' || c == '\\') c = '_'; }
                fs::path dst = fs::path(outdir) / (e.type + "__" + safe);
                std::ofstream o(dst, std::ios::binary);
                o.write(reinterpret_cast<const char*>(e.body_bytes.data()),
                        static_cast<std::streamsize>(e.body_bytes.size()));
                ++ok;
            }
            std::printf("extracted %d entries -> %s\n", ok, outdir.c_str());
            return 0;
        }
        usage();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "milo_tool: %s\n", e.what());
        return 1;
    }
    return 0;
}
