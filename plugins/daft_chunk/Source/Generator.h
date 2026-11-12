#pragma once
// DAFT CHUNK - the idea generator (spec 3). From the slices (their types,
// their pitches, where they sit on the sample's grid), the key, the style,
// CHOP and a seed, it writes a loop of 1, 2 or 4 bars in 16th notes.
//
// CHOP: 0 the slices in their own order; 0.25 a few swaps, repeats on the
// strong beats; 0.5 rearranged, stutters (1/16, 1/32), a little reverse;
// 0.75 tonal slices pitched on the scale, syncopation; 1 wild: glitches,
// rhythmic cuts. The musical rules hold whatever CHOP: beat 1 of every bar
// gets a KICK (or the sample's own beat 1), beats 2 and 4 lean on SNAREs,
// pitched TONAL / VOIX slices stay in the key, and the loop ends on a slice
// that relaunches it (the sample's last one, or a stutter). Locked slices
// keep their steps and are never put anywhere else.
//
// Deterministic to the bit: its own random generator (std:: distributions
// differ between standard libraries), so the same seed gives the same loop
// on every machine. No JUCE.

#include "Analysis.h"
#include <cstdint>
#include <vector>

namespace daftchunk
{
enum class Style { filterTouch, discoLoop, vocalChop, stutterHouse, reverseParty, pitchParty };
inline constexpr int numStyles = 6;
const char* styleName (Style s) noexcept; // "FILTER TOUCH", ...

struct Step
{
    int slice = -1;       // -1: nothing new (what plays goes on)
    int pitch = 0;        // semitones, on top of the slice's own
    int repeat = 1;       // 1, 2 (1/32) or 4 (1/64): a stutter inside the step
    bool reverse = false; // flips the slice's own reverse
    bool cut = false;     // a rhythmic cut: silence from this step (slice -1)
    float velocity = 1.0f;
    bool operator== (const Step& o) const noexcept
    {
        return slice == o.slice && pitch == o.pitch && repeat == o.repeat && reverse == o.reverse && cut == o.cut && ! (velocity < o.velocity) && ! (velocity > o.velocity);
    }
};

struct Pattern
{
    static constexpr int stepsPerBar = 16;
    int bars = 1;
    std::vector<Step> steps; // bars x 16
    uint32_t seed = 0;
    Style style = Style::discoLoop;
    float chop = 0.0f;
    // the style's gesture for the effects (step 5): FILTER TOUCH's filter opening over N bars
    int filterBars = 0;
    bool pump = false;
    int numSteps() const noexcept { return (int) steps.size(); }
    bool operator== (const Pattern& o) const noexcept { return bars == o.bars && steps == o.steps && seed == o.seed; }
};

/** What the generator knows of the sample. */
struct SourceInfo
{
    std::vector<SliceType> types;
    std::vector<float> roots;         // MIDI pitch or -1
    std::vector<bool> locked;         // per slice
    std::vector<int> original;        // per 16th of the sample from beat 1: the slice starting there (-1: none)
    int bars = 1;                     // whole bars in the sample (at least 1)
    Key key;
};

/** The sample's slices on its 16th grid from beat 1 (`sixteenth`: in samples). */
SourceInfo describe (const std::vector<Slice>& slices, const Key& key, double firstBeat, double sixteenth, int64_t numSamples,
                     const std::vector<bool>& locked);

/** The splitmix64 generator: the same numbers everywhere. */
class Random
{
public:
    explicit Random (uint64_t seed) noexcept : state (seed) {}
    uint64_t next() noexcept
    {
        uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    double uniform() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); } // [0, 1)
    int below (int n) noexcept { return n <= 1 ? 0 : (int) (uniform() * n); }
    bool chance (double p) noexcept { return uniform() < p; }

private:
    uint64_t state;
};

/** DÉ: a new loop (bars 0: chosen from the style and CHOP). */
Pattern generate (const SourceInfo& src, Style style, float chop, uint32_t seed, int bars = 0);

/** ENCORE: a close variant of `p`, 10 to 25 % of its steps mutated. */
Pattern mutate (const Pattern& p, const SourceInfo& src, uint32_t seed);
} // namespace daftchunk
