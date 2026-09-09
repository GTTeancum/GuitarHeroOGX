// milo.h - Harmonix .milo container reader (structural pass).
//
// A milo file is a compressed block-structured container holding a tree of
// game objects: meshes, textures, materials, transforms, character data,
// scripts, etc. This reader handles the outer container (compression +
// block layout), inflates payload via miniz, and parses the post-
// decompression object directory enough to enumerate child objects by
// (type, name) -- the structural pass. Deep parsing of individual object
// classes (Mesh / Tex / BandCharacter / ...) is deliberately out of scope
// here and is its own follow-up.
//
// Format reference: PikminGuts92/Mackiloha (MIT), local at
// third_party/Mackiloha/Src/Core/Mackiloha/Milo/MiloFile.cs and
// IO/Serializers/MiloObjectDirSerializer.cs.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gh::milo {

enum class BlockStructure : uint32_t {
    NONE   = 0,
    GZIP   = 1,
    MILO_A = 0xCABEDEAFu,  // structured, no compression (RBN)
    MILO_B = 0xCBBEDEAFu,  // structured, ZLIB blocks   (GH1/GH2/GH80s/RB1/RB2/...)
    MILO_C = 0xCCBEDEAFu,  // structured, GZIP blocks   (Amp, KR1-3)
    MILO_D = 0xCDBEDEAFu,  // structured, ZLIB with 4-byte prefix per block (RB3, DC)
};

const char* block_structure_name(BlockStructure s);

struct Header {
    BlockStructure structure = BlockStructure::NONE;
    uint32_t       first_block_offset = 0;
    uint32_t       block_count = 0;
    uint32_t       max_block_uncompressed_size = 0;
    // Exact table values. MILO_D stores flags in the high byte and disk size
    // in the low 24 bits; the lossless container model retains both.
    std::vector<uint32_t> block_sizes;
};

// Lossless outer-container representation.  `prefix_bytes` retains the complete
// fixed-size 0x210 GH1/GH2 header region, including the unused block-table
// slots whose retail bytes are not reliably zero.  A block can therefore be
// decoded, inspected, and emitted byte-for-byte when it is not changed.
struct ContainerBlock {
    uint32_t table_value = 0;
    bool stored = false;
    std::vector<uint8_t> disk_bytes;
    std::vector<uint8_t> payload_bytes;
    std::vector<uint8_t> original_payload_bytes;
};

struct Container {
    Header header;
    std::vector<uint8_t> prefix_bytes;
    std::vector<ContainerBlock> blocks;
    std::vector<uint8_t> trailing_bytes;
};

struct Entry {
    std::string type;       // e.g. "Mesh", "Tex", "Mat", "Trans"
    std::string name;       // e.g. "head.tex"
    uint64_t    offset = 0; // start in the decompressed payload (after directory header)
    uint64_t    size = 0;   // serialized class body bytes, excluding terminator
    uint64_t    terminator_offset = 0;
    uint32_t    terminator_value = 0;
    std::vector<uint8_t> body_bytes;
};

struct Directory {
    int32_t     dir_version = 0;     // milo "magic" version: 10 (GH1), 24 (GH2), 25 (RB1), ...
    std::string dir_type;            // e.g. "ObjectDir"
    std::string dir_name;            // e.g. "chartest"
    uint32_t    hash_table_hint = 0;
    uint32_t    string_table_hint = 0;
    std::vector<Entry> entries;
    // Revisions 7-16 store external resource paths after the object table.
    // Revision 10 is the packed GH1 form observed by this project.
    std::vector<std::string> external_resources;
    // Exact end of the directory header/table region. For old directories this
    // is the first child body; for revision 17+ it is the root object body.
    uint64_t    object_data_offset = 0;
    // The directory's OWN object body (GH2 version 24+): the bytes between the
    // entry-name list and the first 0xADDEADDE. For a TrackDir/PanelDir this
    // holds the dir's instance properties (y_per_second, slots, top/bottom_y).
    uint64_t    dir_entry_offset = 0;
    uint64_t    dir_entry_size = 0;
    uint32_t    dir_terminator_value = 0;
    std::vector<uint8_t> dir_body_bytes;
    // True only when the complete object-table sequence has one unique
    // terminator chain, every next body begins with the revision required by
    // its declared type, and the final terminator reaches payload EOF.
    bool        boundaries_exact = false;
    uint64_t    payload_end_offset = 0;
    std::vector<uint8_t> trailing_bytes;
};

// Parse the container header only (no decompression).
Header parse_header(const std::vector<uint8_t>& bytes);

// Parse every compressed block while retaining the exact source bytes.
Container parse_container(const std::vector<uint8_t>& bytes);

// Concatenate a parsed container's uncompressed blocks.
std::vector<uint8_t> container_payload(const Container& container);

// Serialize a container. Unchanged blocks retain their original compressed
// bytes; changed MILO_A/MILO_B blocks are encoded deterministically.
std::vector<uint8_t> serialize_container(const Container& container);

// Construct a deterministic GH-style container from an uncompressed payload.
// The default 0x210 prefix is the fixed 128-slot block table used by retail
// GH1/GH2 PS2 files.
Container make_container(const std::vector<uint8_t>& payload,
                         BlockStructure structure = BlockStructure::MILO_B,
                         uint32_t block_uncompressed_limit = 0x20000,
                         uint32_t first_block_offset = 0x210);

// Construct a GH-style object-directory container without splitting a
// serialized object across blocks. Each block may exceed target_block_size so
// that it ends immediately after a 0xADDEADDE object terminator, matching the
// retail GH1/GH2 writer.
Container make_object_aligned_container(
    const std::vector<uint8_t>& payload,
    BlockStructure structure = BlockStructure::MILO_B,
    uint32_t target_block_size = 0x20000,
    uint32_t first_block_offset = 0x210);

// Inflate all blocks and concatenate. For BlockStructure::MILO_B this uses
// raw DEFLATE (no zlib wrapper). For MILO_C it uses GZIP. For MILO_D the
// first 4 bytes of each block are an uncompressed-size prefix before the
// deflate stream. For MILO_A it's just concatenation.
std::vector<uint8_t> inflate_payload(const std::vector<uint8_t>& bytes,
                                     const Header& header);

// Parse the post-decompression object directory. Retail revision-10 GH1
// boundaries are solved and proven as one complete type/revision-constrained
// terminator chain. Generated/custom revision-10 types retain a non-writable
// packed-revision fallback. Later directory revisions retain structural
// scanning until their root and class readers are complete.
Directory parse_directory(
    const std::vector<uint8_t>& payload,
    const std::function<void()>& progress = {});

// Serialize the exact structural prefix through the object table and, for
// revisions 7-16, the external-resource vector. Root/child bodies are not
// emitted by this function.
std::vector<uint8_t> serialize_directory_prefix(const Directory& directory);

// Serialize a complete directory whose framing has been proven exact. This
// covers GH1 revision 10 and GH2 revision 24, preserving/editing every raw
// root/child class body independently while rebuilding object terminators.
std::vector<uint8_t> serialize_directory(const Directory& directory);

// File helper.
std::vector<uint8_t> read_file(const std::string& path);

}  // namespace gh::milo
