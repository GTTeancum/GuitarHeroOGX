#pragma once

#include <array>
#include <cmath>
#include <stdexcept>

namespace gh::milo_convert {

// Row-vector affine matrices. A delta maps source mesh space to target mesh
// space; callers resolve source-only bones to their shared animated ancestor.
inline void retarget_skin_vertex(
    std::array<float, 3>& position, std::array<float, 3>& normal,
    const std::array<float, 4>& weights,
    const std::array<std::array<float, 12>, 4>& deltas) {
    std::array<float, 12> blend{};
    float total = 0;
    for (size_t slot = 0; slot < weights.size(); ++slot) {
        const float weight = weights[slot];
        if (!std::isfinite(weight) || weight < 0)
            throw std::runtime_error("invalid bind-retarget skin weight");
        if (weight == 0) continue;
        total += weight;
        for (size_t i = 0; i < blend.size(); ++i) {
            if (!std::isfinite(deltas[slot][i]))
                throw std::runtime_error("nonfinite bind-retarget matrix");
            blend[i] += weight * deltas[slot][i];
        }
    }
    if (total <= 1.0e-8f)
        throw std::runtime_error("bind-retarget vertex has no influence");
    for (float& value : blend) value /= total;
    std::array<float, 3> point{};
    for (size_t axis = 0; axis < 3; ++axis)
        point[axis] = position[0] * blend[axis] +
            position[1] * blend[3 + axis] +
            position[2] * blend[6 + axis] + blend[9 + axis];

    // Normals use the inverse transpose of the blended linear transform,
    // including nonuniform scale in source body-shape deformation.
    const std::array<float, 9> cofactors = {
        blend[4]*blend[8]-blend[5]*blend[7],
        blend[5]*blend[6]-blend[3]*blend[8],
        blend[3]*blend[7]-blend[4]*blend[6],
        blend[2]*blend[7]-blend[1]*blend[8],
        blend[0]*blend[8]-blend[2]*blend[6],
        blend[1]*blend[6]-blend[0]*blend[7],
        blend[1]*blend[5]-blend[2]*blend[4],
        blend[2]*blend[3]-blend[0]*blend[5],
        blend[0]*blend[4]-blend[1]*blend[3]};
    const float determinant = blend[0]*cofactors[0] +
        blend[1]*cofactors[1] + blend[2]*cofactors[2];
    if (std::abs(determinant) <= 1.0e-8f)
        throw std::runtime_error("singular bind-retarget blend");
    std::array<float, 3> direction{};
    float squared_length = 0;
    for (size_t axis = 0; axis < 3; ++axis) {
        direction[axis] = (normal[0]*cofactors[axis] +
            normal[1]*cofactors[3 + axis] +
            normal[2]*cofactors[6 + axis]) / determinant;
        squared_length += direction[axis]*direction[axis];
    }
    if (squared_length > 1.0e-16f)
        for (float& value : direction) value /= std::sqrt(squared_length);
    position = point;
    normal = direction;
}

} // namespace gh::milo_convert
