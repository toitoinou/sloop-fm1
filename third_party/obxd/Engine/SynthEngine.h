// SLOOP Touch: this file replaces Obxd's SynthEngine.h. The voice's headers include it for what JUCE gave them
// (Random, jmin, jlimit, roundToInt, float_Pi); the plugin itself (SynthEngine, Motherboard, the parameters, the
// editor) is not used: eng_obxd.c drives one ObxdVoice per SLOOP voice (obxd_glue.cc).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

template <typename T> inline T jmin (T a, T b) { return b < a ? b : a; }
template <typename T> inline T jmax (T a, T b) { return a < b ? b : a; }
inline double jmin (double a, float b) { return b < a ? b : a; }
template <typename T> inline T jlimit (T lo, T hi, T v) { return v < lo ? lo : (hi < v ? hi : v); }
inline int roundToInt (float v) { return (int) std::lround (v); }
#include <cstring>
inline void zeromem (void* p, size_t n) { std::memset (p, 0, n); }
namespace juce { const float float_Pi = 3.14159265358979323846f; }

// JUCE's Random (the same linear congruential generator): Obxd's noise and its per-voice "analog" spread
class Random
{
public:
    explicit Random (int64_t s = 1) : seed (s) {}
    static Random& getSystemRandom() { static Random r (0x2545F4914F6CDD1DLL); return r; }
    int nextInt() { seed = (int64_t) (((((uint64_t) seed) * 0x5deece66dLL) + 11) & 0xffffffffffffLL); return (int) (seed >> 16); }
    int64_t nextInt64() { return (int64_t) ((((uint64_t) (unsigned int) nextInt()) << 32) | (uint64_t) (unsigned int) nextInt()); }
    float nextFloat() { return (float) (((unsigned int) nextInt()) / ((float) 0xffffffffu + 1.0f)); }
private:
    int64_t seed;
};
