#pragma once
// Deterministic RNG (SplitMix64) so benchmarks and unit tests are reproducible.
#include <cstdint>

namespace rcai {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ULL) : state_(seed) {}

    std::uint64_t nextU64() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Uniform float in [0, 1).
    float nextFloat() { return (nextU64() >> 40) * (1.0f / 16777216.0f); }

    // Uniform float in [lo, hi).
    float range(float lo, float hi) { return lo + nextFloat() * (hi - lo); }

    // Inclusive integer in [lo, hi].
    int intRange(int lo, int hi) {
        return hi <= lo ? lo : lo + static_cast<int>(nextU64() % static_cast<std::uint64_t>(hi - lo + 1));
    }

    bool chance(float p) { return nextFloat() < p; }

    void reseed(std::uint64_t seed) { state_ = seed; }

private:
    std::uint64_t state_;
};

} // namespace rcai
