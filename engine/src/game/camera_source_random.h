#pragma once

#include "camera_rotation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ghogx::camera {

struct SourceRand {
    uint32_t index_a = 0;
    uint32_t index_b = 0x67;
    std::array<uint32_t, 0x100> values = {};

    void seed(uint32_t seed_value) {
        for (uint32_t i = 0; i < values.size(); ++i) {
            const uint32_t rand_lo = seed_value * 0x41C64E6Du + 0x3039u;
            seed_value = rand_lo * 0x41C64E6Du + 0x3039u;
            values[i] = ((rand_lo >> 16) & 0xFFFFu) | (seed_value & 0x7FFF0000u);
        }
        index_a = 0;
        index_b = 0x67;
    }
    uint32_t next() {
        const uint32_t result = values[index_a] ^ values[index_b];
        values[index_a] = result;
        if (0xF9u <= ++index_a) index_a = 0;
        if (0xF9u <= ++index_b) index_b = 0;
        return result;
    }
    std::size_t int_range(std::size_t high_exclusive) {
        return high_exclusive ? static_cast<std::size_t>(next() % high_exclusive) : 0;
    }
    float unit_float() {
        // GH2 2D9C28: low 16 bits * exact 2^-16 (0x37800000).
        return ee_mul(static_cast<float>(next() & 0xFFFFu),
                      1.0f / 65536.0f);
    }
    float float_range(float low, float high) {
        return ee_add(low, ee_mul(unit_float(), ee_sub(high, low)));
    }
};

} // namespace ghogx::camera
