#pragma once
#include <cmath>

namespace ghogx::ee {
// Emotion Engine scalar rounding used by the recovered camera/crowd code.
inline float ee_float(double value) {
    const float nearest = static_cast<float>(value);
    if (!std::isfinite(value) || !std::isfinite(nearest) || nearest == 0.0f)
        return nearest;
    if (std::abs(static_cast<double>(nearest)) > std::abs(value))
        return std::nextafter(nearest, 0.0f);
    return nearest;
}
inline float ee_add(float a, float b) { return ee_float(static_cast<double>(a) + b); }
inline float ee_sub(float a, float b) { return ee_float(static_cast<double>(a) - b); }
inline float ee_mul(float a, float b) { return ee_float(static_cast<double>(a) * b); }
// The retained PCSX2 retail oracle uses nearest rounding for scalar DIV.S,
// separately from chopped ADD/SUB/MUL. Chopping division as well loses an
// authored crowd member in Basement region 5 (the nearly vertical triangle).
// Keep this separate: existing camera helpers do not use this division path.
inline float ee_div(float a, float b) { return static_cast<float>(static_cast<double>(a) / b); }
}
