#include "milo.h"

#include <exception>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

void append_u32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 24) & 0xffu));
}

void append_string(std::vector<uint8_t>& bytes, const char* value) {
    const size_t len = std::strlen(value);
    append_u32(bytes, static_cast<uint32_t>(len));
    bytes.insert(bytes.end(), value, value + len);
}

}  // namespace

int main() {
    std::vector<uint8_t> bytes;
    append_u32(bytes, static_cast<uint32_t>(gh::milo::BlockStructure::MILO_B));
    append_u32(bytes, 20);  // First block follows the 16-byte header + one size.
    append_u32(bytes, 1);
    append_u32(bytes, 0);
    append_u32(bytes, 2);
    bytes.push_back(0x03);
    bytes.push_back(0x00);

    try {
        const auto header = gh::milo::parse_header(bytes);
        const auto payload = gh::milo::inflate_payload(bytes, header);
        if (!payload.empty()) {
            std::fprintf(stderr,
                         "milo_test: empty raw deflate block produced %zu bytes\n",
                         payload.size());
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
                     "milo_test: empty raw deflate block failed: %s\n",
                     ex.what());
        return 1;
    }

    // A no-edit container pass must preserve every source byte, including
    // fixed-table residual bytes and the original compressed representation.
    try {
        const auto container = gh::milo::parse_container(bytes);
        const auto serialized = gh::milo::serialize_container(container);
        if (serialized != bytes) {
            std::fprintf(stderr, "milo_test: unchanged container was not byte-exact\n");
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "milo_test: lossless container pass failed: %s\n",
                     ex.what());
        return 1;
    }

    // A changed MILO_B block must be encoded as valid raw DEFLATE and decode
    // to exactly the edited payload.
    try {
        auto container = gh::milo::parse_container(bytes);
        container.blocks[0].payload_bytes = {0x10, 0x20, 0x30, 0x40, 0x50};
        const auto serialized = gh::milo::serialize_container(container);
        const auto reparsed = gh::milo::parse_container(serialized);
        if (gh::milo::container_payload(reparsed) !=
            container.blocks[0].payload_bytes) {
            std::fprintf(stderr, "milo_test: edited block did not round-trip\n");
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "milo_test: edited block encode failed: %s\n",
                     ex.what());
        return 1;
    }

    // Deterministic construction must preserve payloads spanning several
    // compressed blocks.
    try {
        std::vector<uint8_t> source(1000);
        for (size_t i = 0; i < source.size(); ++i)
            source[i] = static_cast<uint8_t>((i * 37u) & 0xffu);
        const auto made = gh::milo::make_container(
            source, gh::milo::BlockStructure::MILO_B, 127);
        const auto serialized = gh::milo::serialize_container(made);
        const auto reparsed = gh::milo::parse_container(serialized);
        if (gh::milo::container_payload(reparsed) != source ||
            reparsed.blocks.size() != 8) {
            std::fprintf(stderr, "milo_test: constructed container mismatch\n");
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "milo_test: constructed container failed: %s\n",
                     ex.what());
        return 1;
    }

    // Retail GH1/GH2 blocks accumulate complete serialized objects and may
    // exceed the nominal target instead of cutting through an object body.
    try {
        std::vector<uint8_t> source;
        source.insert(source.end(), 70000, 0x11);
        append_u32(source, 0xDEADDEAD);
        source.insert(source.end(), 70000, 0x22);
        append_u32(source, 0xDEADDEAD);
        source.insert(source.end(), 100, 0x33);
        append_u32(source, 0xDEADDEAD);

        const auto made = gh::milo::make_object_aligned_container(source);
        if (made.blocks.size() != 2 ||
            made.blocks[0].payload_bytes.size() != 140008 ||
            made.blocks[1].payload_bytes.size() != 104 ||
            made.header.max_block_uncompressed_size != 140008) {
            std::fprintf(stderr,
                         "milo_test: object-aligned block sizing mismatch\n");
            return 1;
        }
        const auto serialized = gh::milo::serialize_container(made);
        const auto reparsed = gh::milo::parse_container(serialized);
        if (gh::milo::container_payload(reparsed) != source) {
            std::fprintf(stderr,
                         "milo_test: object-aligned container did not round-trip\n");
            return 1;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr,
                     "milo_test: object-aligned construction failed: %s\n",
                     ex.what());
        return 1;
    }

    // Revision-10 directories have no root-directory body. Verify that the
    // first table entry receives the first body rather than being discarded
    // at its DEADDEAD terminator and shifting every subsequent association.
    std::vector<uint8_t> gh1;
    append_u32(gh1, 10);
    append_u32(gh1, 2);
    append_string(gh1, "Tex"); append_string(gh1, "first.tex");
    append_string(gh1, "Mat"); append_string(gh1, "first.mat");
    append_u32(gh1, 1);         // external resource count
    append_string(gh1, "external.milo_ps2");
    const size_t first_body = gh1.size();
    append_u32(gh1, 8); append_u32(gh1, 0x11111111);
    append_u32(gh1, 0xDEADDEAD);
    const size_t second_body = gh1.size();
    append_u32(gh1, 21); append_u32(gh1, 0x22222222);
    append_u32(gh1, 0xDEADDEAD);
    const auto gh1_dir = gh::milo::parse_directory(gh1);
    if (gh1_dir.entries.size() != 2 ||
        gh1_dir.external_resources !=
            std::vector<std::string>({"external.milo_ps2"}) ||
        gh1_dir.object_data_offset != first_body ||
        gh::milo::serialize_directory_prefix(gh1_dir) !=
            std::vector<uint8_t>(gh1.begin(), gh1.begin() + first_body) ||
        gh1_dir.dir_entry_size != 0 ||
        gh1_dir.entries[0].offset != first_body ||
        gh1_dir.entries[0].size != 8 ||
        gh1_dir.entries[0].terminator_offset != first_body + 8 ||
        gh1_dir.entries[1].offset != second_body ||
        gh1_dir.entries[1].size != 8 ||
        gh1_dir.entries[1].terminator_offset != second_body + 8 ||
        gh1_dir.entries[0].body_bytes !=
            std::vector<uint8_t>(gh1.begin() + first_body,
                                 gh1.begin() + first_body + 8) ||
        !gh1_dir.boundaries_exact ||
        gh1_dir.payload_end_offset != gh1.size() ||
        gh::milo::serialize_directory(gh1_dir) != gh1) {
        std::fprintf(stderr, "milo_test: GH1 child-body alignment failed\n");
        return 1;
    }
    auto gh1_edited = gh1_dir;
    gh1_edited.entries[0].name = "renamed.tex";
    gh1_edited.entries[0].body_bytes[4] ^= 0x5a;
    const auto gh1_edited_bytes =
        gh::milo::serialize_directory(gh1_edited);
    const auto gh1_edited_dir =
        gh::milo::parse_directory(gh1_edited_bytes);
    if (gh1_edited_dir.entries[0].name != "renamed.tex" ||
        gh1_edited_dir.entries[0].body_bytes !=
            gh1_edited.entries[0].body_bytes ||
        gh::milo::serialize_directory(gh1_edited_dir) !=
            gh1_edited_bytes) {
        std::fprintf(stderr, "milo_test: GH1 directory edit failed\n");
        return 1;
    }

    // Revision 10 is also used by generated/custom directories containing
    // classes outside the retail GH1 inventory. Read those structurally using
    // plausible packed revisions, but do not claim exact writable boundaries.
    std::vector<uint8_t> custom10;
    append_u32(custom10, 10);
    append_u32(custom10, 1);
    append_string(custom10, "LabelEx");
    append_string(custom10, "line1.lbl");
    append_u32(custom10, 0);  // external resources
    const size_t custom_body = custom10.size();
    append_u32(custom10, 15);
    append_u32(custom10, 0x12345678);
    append_u32(custom10, 0xDEADDEAD);
    const auto custom10_dir = gh::milo::parse_directory(custom10);
    if (custom10_dir.entries.size() != 1 ||
        custom10_dir.entries[0].offset != custom_body ||
        custom10_dir.entries[0].size != 8 ||
        custom10_dir.boundaries_exact) {
        std::fprintf(stderr,
                     "milo_test: custom revision-10 fallback failed\n");
        return 1;
    }

    // Revision-24 prefixes include root type/name and allocation hints, but
    // stop before the root object's own serialized body.
    std::vector<uint8_t> gh2;
    append_u32(gh2, 24);
    append_string(gh2, "RndDir");
    append_string(gh2, "venue");
    append_u32(gh2, 17);
    append_u32(gh2, 33);
    append_u32(gh2, 1);
    append_string(gh2, "Mat");
    append_string(gh2, "stage.mat");
    const size_t gh2_root = gh2.size();
    append_u32(gh2, 8);
    append_u32(gh2, 0x11111111);
    append_u32(gh2, 0xDEADDEAD);
    append_u32(gh2, 21);
    append_u32(gh2, 0xDEADDEAD);  // legal body data, not its terminator
    append_u32(gh2, 7);           // plausible revision after false marker
    append_u32(gh2, 0x22222222);
    append_u32(gh2, 0xDEADDEAD);
    const auto gh2_dir = gh::milo::parse_directory(gh2);
    if (gh2_dir.dir_type != "RndDir" ||
        gh2_dir.dir_name != "venue" ||
        gh2_dir.hash_table_hint != 17 ||
        gh2_dir.string_table_hint != 33 ||
        gh2_dir.object_data_offset != gh2_root ||
        gh::milo::serialize_directory_prefix(gh2_dir) !=
            std::vector<uint8_t>(gh2.begin(), gh2.begin() + gh2_root) ||
        gh2_dir.dir_body_bytes !=
            std::vector<uint8_t>(gh2.begin() + gh2_root,
                                 gh2.begin() + gh2_root + 8) ||
        gh2_dir.dir_terminator_value != 0xDEADDEAD ||
        gh2_dir.entries[0].body_bytes.size() != 16 ||
        !gh2_dir.boundaries_exact ||
        gh::milo::serialize_directory(gh2_dir) != gh2) {
        std::fprintf(stderr, "milo_test: GH2 directory prefix failed\n");
        return 1;
    }

    // A normal large directory has one terminator per declared object. Its
    // recovery work must stay bounded; the old suffix search exhausts this
    // cooperative-work budget long before it finishes this fixture.
    const auto make_modern_prefix = [](uint32_t count) {
        std::vector<uint8_t> data;
        append_u32(data, 24);
        append_string(data, "RndDir");
        append_string(data, "boundary_test");
        append_u32(data, 0);
        append_u32(data, 0);
        append_u32(data, count);
        for (uint32_t i = 0; i < count; ++i) {
            append_string(data, "Mat");
            const auto name = "material_" + std::to_string(i);
            append_string(data, name.c_str());
        }
        append_u32(data, 8);
        append_u32(data, 0xDEADDEAD);
        return data;
    };
    auto large_directory = make_modern_prefix(1000);
    for (uint32_t i = 0; i < 1000; ++i) {
        append_u32(large_directory, 21);
        append_u32(large_directory, 0x11111111);
        append_u32(large_directory, 0xDEADDEAD);
    }
    auto large_embedded_marker = large_directory;
    const size_t first_large_body = make_modern_prefix(1000).size();
    std::vector<uint8_t> embedded_data;
    append_u32(embedded_data, 0xDEADDEAD);
    append_u32(embedded_data, 0x11111111); // not a packed object revision
    large_embedded_marker.insert(
        large_embedded_marker.begin() + first_large_body + 4,
        embedded_data.begin(), embedded_data.end());
    try {
      for (const auto* fixture : {&large_directory, &large_embedded_marker}) {
        size_t work_callbacks = 0;
        const auto large = gh::milo::parse_directory(*fixture, [&]() {
            if (++work_callbacks > 32)
                throw std::runtime_error("superlinear directory boundary work");
        });
        if (!large.boundaries_exact || large.entries.size() != 1000 ||
            gh::milo::serialize_directory(large) != *fixture)
            throw std::runtime_error("large directory did not round-trip exactly");
      }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "milo_test: large boundary recovery: %s\n", ex.what());
        return 1;
    }

    // Two valid partitions remain ambiguous. Pruning must neither claim a
    // unique parse nor change the first partition chosen by the old solver.
    auto ambiguous_directory = make_modern_prefix(2);
    const size_t ambiguous_start = ambiguous_directory.size();
    for (uint32_t i = 0; i < 3; ++i) {
        append_u32(ambiguous_directory, 21);
        append_u32(ambiguous_directory, 0xDEADDEAD);
    }
    const auto ambiguous = gh::milo::parse_directory(ambiguous_directory);
    if (ambiguous.boundaries_exact || ambiguous.entries.size() != 2 ||
        ambiguous.entries[0].offset != ambiguous_start ||
        ambiguous.entries[0].body_bytes.size() != 4 ||
        ambiguous.entries[1].body_bytes.size() != 12) {
        std::fprintf(stderr, "milo_test: ambiguous boundary recovery changed\n");
        return 1;
    }

    bool rejected_ambiguous_write = false;
    try {
        (void)gh::milo::serialize_directory(ambiguous);
    } catch (const std::runtime_error&) {
        rejected_ambiguous_write = true;
    }
    if (!rejected_ambiguous_write) {
        std::fprintf(stderr, "milo_test: ambiguous directory was serializable\n");
        return 1;
    }
    // Runtime consumers share one inflate+directory parse while assembling a
    // screen/world, then explicitly release it at the lifecycle boundary.
    const auto cache_container =
        gh::milo::make_container(gh1, gh::milo::BlockStructure::MILO_A);
    const auto cache_bytes = gh::milo::serialize_container(cache_container);
    const auto cache_header = gh::milo::parse_header(cache_bytes);
    const auto cached_a = gh::milo::inflate_directory_cached(
        "milo_test/cache", cache_bytes, cache_header);
    const auto cached_b = gh::milo::inflate_directory_cached(
        "milo_test/cache", cache_bytes, cache_header);
    if (cached_a != cached_b || cached_a->payload != gh1 ||
        cached_a->directory.entries.size() != 2) {
        std::fprintf(stderr, "milo_test: parsed-directory cache failed\n");
        return 1;
    }
    gh::milo::clear_inflated_directory_cache();
    const auto cached_after_clear = gh::milo::inflate_directory_cached(
        "milo_test/cache", cache_bytes, cache_header);
    if (cached_after_clear == cached_a) {
        std::fprintf(stderr,
                     "milo_test: parsed-directory cache clear failed\n");
        return 1;
    }
    gh::milo::clear_inflated_directory_cache();

    std::printf("milo_test: all checks passed\n");
    return 0;
}
