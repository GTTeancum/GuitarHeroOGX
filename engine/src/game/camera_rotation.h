#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include "../core/ee_float.h"

namespace ghogx::camera {
// The Emotion Engine FPU runs these camera helpers with scalar results chopped
// toward zero.  A host float cast uses round-to-nearest, so reproduce the
// source operation by stepping one representable value toward zero whenever
// that cast overshoots the exact finite result.
using ee::ee_float;
using ee::ee_add;
using ee::ee_sub;
using ee::ee_mul;

inline void source_shake_spring_step(
    const std::array<float, 3>& target,
    std::array<float, 3>& output,
    std::array<float, 3>& velocity) {
    // GH2 USA CamShot::Shake 0x00263170..0x002632E4.  Retail normalizes the
    // target/output delta, then multiplies it by length * 0.02.  This is a
    // fixed 2% displacement correction; multiplying the unnormalized delta by
    // length again would incorrectly make the spring strength distance-squared.
    const std::array<float, 3> diff = {
        ee_sub(target[0], output[0]),
        ee_sub(target[1], output[1]),
        ee_sub(target[2], output[2])};
    const float length = std::sqrt(
        diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
    std::array<float, 3> correction{};
    if (length > 0.0f && std::isfinite(length)) {
        const float scaled_length = ee_mul(length, 0.02f);
        for (int axis = 0; axis < 3; ++axis) {
            correction[axis] = ee_mul(diff[axis] / length, scaled_length);
        }
    }
    for (int axis = 0; axis < 3; ++axis) {
        output[axis] = ee_add(output[axis], velocity[axis]);
        velocity[axis] = ee_add(velocity[axis], correction[axis]);
        output[axis] = ee_add(output[axis], correction[axis]);
        velocity[axis] = ee_mul(velocity[axis], 0.9f);
    }
}

// GH2 USA 0x2DC500 -> 0x2DC4B8.  The executable uses a 64-interval,
// linearly interpolated quarter-wave table rather than libm.  Retain the
// literal float table and the source scalar-operation order because even
// cos(0) is 0.999698877, and MakeRotMatrix consequently returns a basis with
// a small, source-authored uniform scale.
inline constexpr std::array<float, 65> kSourceSineValues = {
    0.0f, 0.024541229009628296f, 0.049067676067352295f,
    0.0735645666718483f, 0.0980171412229538f, 0.12241066992282867f,
    0.1467304676771164f, 0.1709618866443634f, 0.19509032368659973f,
    0.21910123527050018f, 0.24298018217086792f, 0.2667127549648285f,
    0.290284663438797f, 0.3136817216873169f, 0.3368898332118988f,
    0.3598950505256653f, 0.3826834559440613f, 0.40524131059646606f,
    0.4275550842285156f, 0.4496113359928131f, 0.47139671444892883f,
    0.49289819598197937f, 0.5141027569770813f, 0.5349976420402527f,
    0.5555702447891235f, 0.5758082270622253f, 0.5956992506980896f,
    0.6152315735816956f, 0.6343932747840881f, 0.6531728506088257f,
    0.6715589761734009f, 0.6895405650138855f, 0.7071068286895752f,
    0.7242470979690552f, 0.7409511208534241f, 0.7572088837623596f,
    0.7730104923248291f, 0.7883464097976685f, 0.8032075762748718f,
    0.8175848126411438f, 0.8314695954322815f, 0.8448535799980164f,
    0.8577286005020142f, 0.8700870275497437f, 0.8819212317466736f,
    0.89322429895401f, 0.903989315032959f, 0.9142097234725952f,
    0.9238795042037964f, 0.9329928159713745f, 0.9415441155433655f,
    0.949528157711029f, 0.9569403529167175f, 0.9637761116027832f,
    0.9700312614440918f, 0.975702166557312f, 0.9807853102684021f,
    0.9852776527404785f, 0.9891765117645264f, 0.9924795627593994f,
    0.9951847791671753f, 0.9972904920578003f, 0.9987955093383789f,
    0.9996988773345947f, 1.0f};

inline constexpr std::array<float, 65> kSourceSineSlopes = {
    0.024541229009628296f, 0.024526447057724f, 0.024496890604496002f,
    0.0244525745511055f, 0.024393528699874878f, 0.02431979775428772f,
    0.02423141896724701f, 0.024128437042236328f, 0.02401091158390045f,
    0.023878946900367737f, 0.02373257279396057f, 0.023571908473968506f,
    0.023397058248519897f, 0.02320811152458191f, 0.02300521731376648f,
    0.022788405418395996f, 0.022557854652404785f, 0.02231377363204956f,
    0.022056251764297485f, 0.021785378456115723f, 0.021501481533050537f,
    0.02120456099510193f, 0.020894885063171387f, 0.02057260274887085f,
    0.020237982273101807f, 0.019891023635864258f, 0.019532322883605957f,
    0.019161701202392578f, 0.01877957582473755f, 0.018386125564575195f,
    0.01798158884048462f, 0.017566263675689697f, 0.01714026927947998f,
    0.016704022884368896f, 0.016257762908935547f, 0.015801608562469482f,
    0.015335917472839355f, 0.01486116647720337f, 0.014377236366271973f,
    0.013884782791137695f, 0.013383984565734863f, 0.012875020503997803f,
    0.012358427047729492f, 0.011834204196929932f, 0.011303067207336426f,
    0.010765016078948975f, 0.01022040843963623f, 0.009669780731201172f,
    0.009113311767578125f, 0.008551299571990967f, 0.007984042167663574f,
    0.0074121952056884766f, 0.006835758686065674f, 0.006255149841308594f,
    0.005670905113220215f, 0.005083143711090088f, 0.004492342472076416f,
    0.0038988590240478516f, 0.003303050994873047f, 0.002705216407775879f,
    0.002105712890625f, 0.0015050172805786133f, 0.0009033679962158203f,
    0.0f, 0.0f};

inline float source_sine(float angle) {
    constexpr float kTwoOverPi = 0.6366197466850281f; // 0x3F22F983
    constexpr float kHalfPi = 1.5707963705062866f;    // 0x3FC90FDB
    constexpr float kTableScale = 40.7436637878418f;
    constexpr int kIntervals = 64;

    if (!std::isfinite(angle)) return std::numeric_limits<float>::quiet_NaN();
    int quadrant = static_cast<int>(std::trunc(ee_mul(angle, kTwoOverPi)));
    float remainder = ee_sub(angle, ee_mul(static_cast<float>(quadrant), kHalfPi));
    if (remainder < 0.0f) {
        remainder = ee_add(remainder, kHalfPi);
        --quadrant;
    }
    float coordinate = ee_mul(remainder, kTableScale);
    if ((quadrant & 1) != 0) {
        coordinate = ee_sub(static_cast<float>(kIntervals), coordinate);
    }
    // Camera shake angles are small, but keep malformed inputs memory-safe.
    const int index = std::clamp(static_cast<int>(std::trunc(coordinate)),
                                 0, kIntervals);
    const float fraction = ee_sub(coordinate, static_cast<float>(index));
    const float value = ee_add(
        kSourceSineValues[static_cast<std::size_t>(index)],
        ee_mul(fraction, kSourceSineSlopes[static_cast<std::size_t>(index)]));
    return (quadrant & 2) != 0 ? -value : value;
}

inline float source_cosine(float angle) {
    constexpr float kHalfPi = 1.5707963705062866f;
    return source_sine(ee_add(angle, kHalfPi));
}

inline std::array<std::array<float, 3>, 3> source_euler_rotation(
    const std::array<float, 3>& euler) {
    // GH2 MakeRotMatrix(Vector3, Matrix3, true), 0x2DA888..0x2DA988.
    // Keep the executable's instruction order, including every chopped scalar
    // multiply/add/subtract, instead of relying on host expression fusion.
    const float sx = source_sine(euler[0]);
    const float cx = source_cosine(euler[0]);
    const float sy = source_sine(euler[1]);
    const float cy = source_cosine(euler[1]);
    const float sz = source_sine(euler[2]);
    const float cz = source_cosine(euler[2]);

    const float cy_cz = ee_mul(cy, cz);
    const float sy_sz = ee_mul(sy, sz);
    const float cy_sz = ee_mul(cy, sz);
    const float cz_sy = ee_mul(cz, sy);
    const float cy_cz_sx = ee_mul(cy_cz, sx);
    const float cy_sz_sx = ee_mul(cy_sz, sx);
    const float sy_sz_sx = ee_mul(sy_sz, sx);
    const float cz_sy_sx = ee_mul(cz_sy, sx);

    return {{{ee_sub(cy_cz, sy_sz_sx),
              ee_add(cy_sz, cz_sy_sx),
              ee_mul(-sy, cx)},
             {ee_mul(-cx, sz), ee_mul(cx, cz), sx},
             {ee_add(cz_sy, cy_sz_sx),
              ee_sub(sy_sz, cy_cz_sx),
              ee_mul(cy, cx)}}};
}

// GH2 Matrix3 -> Quat at 2DA318..2DA570. Preserve the quaternion's magnitude:
// the source returns directly without normalization. This matters for authored
// matrices with slight scale/skew, including the original saved lose01 frame.
inline std::array<float, 4> quat_from_row_matrix(
    const std::array<std::array<float, 3>, 3>& m) {
    const float r00=m[0][0], r01=m[1][0], r02=m[2][0];
    const float r10=m[0][1], r11=m[1][1], r12=m[2][1];
    const float r20=m[0][2], r21=m[1][2], r22=m[2][2];
    const float trace = r00+r11+r22;
    std::array<float,4> q{};
    if (trace > 0) {
        const float root = std::sqrt(trace+1);
        const float scale = 0.5f/root;
        q = {(r21-r12)*scale, (r02-r20)*scale, (r10-r01)*scale, root*0.5f};
    } else {
        // 2DA3F0..2DA444: strict comparisons keep the FIRST largest diagonal
        // on ties. The usual three-branch conversion picks the last instead,
        // which is not equivalent for the raw scaled/skewed source matrices.
        int i = m[0][0] < m[1][1] ? 1 : 0;
        if (m[i][i] < m[2][2]) i = 2;
        const int j = (i + 1) % 3, k = (j + 1) % 3;
        const float root = std::sqrt(((m[i][i] - m[j][j]) - m[k][k]) + 1.0f);
        q[i] = root * 0.5f;
        const float scale = root == 0.0f ? root : 0.5f / root;
        q[3] = (m[j][k] - m[k][j]) * scale;
        q[j] = (m[i][j] + m[j][i]) * scale;
        q[k] = (m[i][k] + m[k][i]) * scale;
    }
    for (float value : q) if (!std::isfinite(value)) return {0,0,0,1};
    return q;
}

// GH2 PS2 Interp(Quat), 0x2da6d0..0x2da804. The Matrix3 overload
// at 0x2da808 converts both matrices, calls this, then rebuilds the matrix.
// Hemisphere-correct normalized lerp, NOT independent forward/up lerps.
inline std::array<float, 4> interp_quat(const std::array<float, 4>& a,
                                        const std::array<float, 4>& b, float t) {
    if (t == 0.0f) return a;
    if (t == 1.0f) return b;
    float dot = 0.0f;
    for (int i = 0; i < 4; ++i) dot += a[i] * b[i];
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    std::array<float, 4> q{};
    float norm2 = 0.0f;
    for (int i = 0; i < 4; ++i) {
        q[i] = a[i] + (b[i] * sign - a[i]) * t;
        norm2 += q[i] * q[i];
    }
    if (!std::isfinite(norm2) || norm2 <= 1.0e-12f) return a;
    const float norm = std::sqrt(norm2);
    for (auto& value : q) value /= norm;
    return q;
}
// GH2 USA 2DAA30..2DAACC: no normalization, including endpoint quaternions
// returned unchanged by Interp. Match the scalar operation order as well.
inline std::array<std::array<float, 3>, 3> row_matrix_from_quat(
    const std::array<float, 4>& q) {
    const float x2 = ee_add(q[0], q[0]);
    const float y2 = ee_add(q[1], q[1]);
    const float z2 = ee_add(q[2], q[2]);
    const float xx = ee_mul(x2, q[0]), yy = ee_mul(y2, q[1]);
    const float zz = ee_mul(z2, q[2]), xy = ee_mul(x2, q[1]);
    const float xz = ee_mul(x2, q[2]), yz = ee_mul(y2, q[2]);
    const float xw = ee_mul(x2, q[3]), yw = ee_mul(y2, q[3]);
    const float zw = ee_mul(z2, q[3]);
    return {{{ee_sub(ee_sub(1.0f, yy), zz), ee_add(xy, zw), ee_sub(xz, yw)},
             {ee_sub(xy, zw), ee_sub(ee_sub(1.0f, zz), xx), ee_add(yz, xw)},
             {ee_add(xz, yw), ee_sub(yz, xw), ee_sub(ee_sub(1.0f, xx), yy)}}};
}

inline std::array<std::array<float, 3>, 3> interp_row_matrix(
    const std::array<std::array<float, 3>, 3>& a,
    const std::array<std::array<float, 3>, 3>& b, float t) {
    // Matrix3 Interp 2DA808 always performs both conversions, even at 0/1.
    // Returning a/b directly is observably different for authored scale/skew.
    return row_matrix_from_quat(interp_quat(quat_from_row_matrix(a),
                                           quat_from_row_matrix(b), t));
}
} // namespace ghogx::camera
