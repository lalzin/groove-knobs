// CRACKLE — the saturation (in the RC-20's spirit: a few characters, not one
// tanh): six TYPES and a FOCUS band. Runs inside the 2x oversampled stage, one
// instance per channel.
//
//   TUBE         a valve: asymmetric (even harmonics), round
//   TRANSFORMER  its core saturates on the lows first: thick, warm
//   SPEAKER      a blown speaker: a narrow band, hard asymmetric clipping, the
//                cone rattling when pushed
//   TAPE         soft, the highs compress first (pre / de-emphasis)
//   FOLD         a wavefolder: metallic, rich
//   HARD         a hard clip with a short knee: digital crunch
//
// FOCUS: only the band between its low and high edges is saturated, the rest
// passes clean (both edges open: everything).
#pragma once
#include <cstdint>

namespace crackle
{
enum class SatType { tube, transformer, speaker, tape, fold, hard, count };

class Saturator
{
public:
    /** rate: the oversampled rate (2x). */
    void prepare (double rate);
    void reset();
    /** amount 0..1 (0: exactly linear), focus edges in Hz. */
    double process (double x, SatType type, double amount, double focusLowHz, double focusHighHz) noexcept;

    static const char* name (SatType t);

private:
    struct Svf
    {
        double ic1 = 0, ic2 = 0;
        double run (double x, double g, double k, double& lp, double& bp, double& hp) noexcept;
    };
    struct OnePole
    {
        double z = 0, c = 0;
        double lp (double x) noexcept { z += c * (x - z); return z; }
    };
    double core (double x, SatType type, double drive) noexcept;
    double fs = 96000.0;
    Svf focusHp, focusLp, speakerHp, speakerLp;
    OnePole lowPre, lowPost, highPre, highPost;
    std::uint32_t seed = 0x1f2e3d4cu;
    double gFocusLo = 0.0, gFocusHi = 0.0, lastLo = -1.0, lastHi = -1.0;
};
} // namespace crackle
