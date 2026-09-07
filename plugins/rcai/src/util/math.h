#pragma once
// Portable math helpers for the RCAI core (no platform deps).
#include <cmath>

namespace rcai {

struct Vec2 {
    float x = 0.f, z = 0.f; // ground plane
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.z + b.z}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.z - b.z}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.z * s}; }
inline Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.z * s}; }

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.z * b.z; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline float distance(Vec2 a, Vec2 b) { return length(a - b); }

inline Vec2 normalize(Vec2 a) {
    const float len = length(a);
    return len > 1e-6f ? Vec2{a.x / len, a.z / len} : Vec2{1.f, 0.f};
}

inline float cross2(Vec2 a, Vec2 b) { return a.x * b.z - a.z * b.x; }

// Angle in [-pi, pi] from a to b.
inline float angleBetween(Vec2 a, Vec2 b) {
    const float c = dot(a, b) / (length(a) * length(b) + 1e-9f);
    const float s = cross2(a, b);
    return std::atan2(s, c < -1.f ? -1.f : (c > 1.f ? 1.f : c));
}

inline float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Segment-segment intersection test (2D), used for line-of-sight.
inline bool segmentsIntersect(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4) {
    const Vec2 d1 = p2 - p1, d2 = p4 - p3;
    const float denom = cross2(d1, d2);
    if (std::fabs(denom) < 1e-9f) return false; // parallel
    const float t = cross2(p3 - p1, d2) / denom;
    const float u = cross2(p3 - p1, d1) / denom;
    return t > 1e-6f && t < 1.f - 1e-6f && u > 1e-6f && u < 1.f - 1e-6f;
}

} // namespace rcai
