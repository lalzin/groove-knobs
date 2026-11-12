#include "Generator.h"
#include <algorithm>
#include <cmath>

namespace daftchunk
{
namespace
{
    enum Role { kickRole, snareRole, hatRole, otherRole };

    Role roleOf (SliceType t) noexcept
    {
        switch (t)
        {
            case SliceType::kick: return kickRole;
            case SliceType::snare: return snareRole;
            case SliceType::hat: return hatRole;
            case SliceType::voice:
            case SliceType::tonal:
            case SliceType::noise: break;
        }
        return otherRole;
    }

    double smooth (double a, double b, double x) noexcept
    {
        const double t = std::clamp ((x - a) / (b - a), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    struct Pools
    {
        std::vector<int> byRole[4], all, tonal, voice;
    };

    Pools poolsOf (const SourceInfo& src)
    {
        Pools p;
        for (int i = 0; i < (int) src.types.size(); ++i)
        {
            if (i < (int) src.locked.size() && src.locked[(size_t) i])
                continue; // a locked slice is never put anywhere else
            p.byRole[roleOf (src.types[(size_t) i])].push_back (i);
            p.all.push_back (i);
            if (src.types[(size_t) i] == SliceType::tonal)
                p.tonal.push_back (i);
            if (src.types[(size_t) i] == SliceType::voice)
                p.voice.push_back (i);
        }
        return p;
    }

    int pick (const std::vector<int>& pool, Random& r) noexcept { return pool.empty() ? -1 : pool[(size_t) r.below ((int) pool.size())]; }

    bool isLocked (const SourceInfo& src, int slice) noexcept
    {
        return slice >= 0 && slice < (int) src.locked.size() && src.locked[(size_t) slice];
    }

    Role roleAt (const SourceInfo& src, int slice) noexcept
    {
        return slice >= 0 && slice < (int) src.types.size() ? roleOf (src.types[(size_t) slice]) : otherRole;
    }

    bool pitched (const SourceInfo& src, int slice) noexcept
    {
        if (slice < 0 || slice >= (int) src.types.size())
            return false;
        const auto t = src.types[(size_t) slice];
        return t == SliceType::tonal || t == SliceType::voice;
    }

    /** The semitone offset that puts slice `slice` on scale degree `degree`: degree 0 is the
        scale note nearest the slice's own pitch, +7 an octave up (so a melody stays around it). */
    int scaleOffset (const SourceInfo& src, int slice, int degree) noexcept
    {
        const auto scale = src.key.scale(); // pitch classes, tonic first
        const float root = slice >= 0 && slice < (int) src.roots.size() ? src.roots[(size_t) slice] : -1.0f;
        const int rootPc = root >= 0.0f ? ((int) std::lround (root) % 12 + 12) % 12 : src.key.tonic;
        // the scale degree nearest the root
        int near0 = 0, best = 99;
        for (int d = 0; d < 7; ++d)
        {
            int diff = ((scale[(size_t) d] - rootPc) % 12 + 12) % 12;
            if (diff > 6)
                diff -= 12;
            if (std::abs (diff) < best)
            {
                best = std::abs (diff);
                near0 = d;
            }
        }
        // semitones above the tonic of scale degree d (any d: octaves included)
        auto semis = [&] (int d) { const int o = (int) std::floor (d / 7.0); return ((scale[(size_t) (((d % 7) + 7) % 7)] - scale[0]) % 12 + 12) % 12 + 12 * o; };
        int nearOff = ((scale[(size_t) near0] - rootPc) % 12 + 12) % 12;
        if (nearOff > 6)
            nearOff -= 12;
        const int off = nearOff + semis (near0 + degree) - semis (near0);
        return std::clamp (off, -24, 24);
    }

    /** A pool of melodic slices: the voices, topped up with tones, then the rest (3 at least). */
    std::vector<int> melodicPool (const Pools& p, bool preferTonal)
    {
        std::vector<int> pool = preferTonal ? p.tonal : p.voice;
        for (const auto* more : { preferTonal ? &p.voice : &p.tonal, &p.byRole[3], &p.all })
            if (pool.size() < 3)
                for (int i : *more)
                    if (std::find (pool.begin(), pool.end(), i) == pool.end())
                        pool.push_back (i);
        return pool;
    }

    /** Up to `count` different slices from `pool`. */
    std::vector<int> palette (std::vector<int> pool, int count, Random& r)
    {
        std::vector<int> out;
        while ((int) out.size() < count && ! pool.empty())
        {
            const int i = r.below ((int) pool.size());
            out.push_back (pool[(size_t) i]);
            pool.erase (pool.begin() + i);
        }
        return out;
    }

    /** One-bar rhythms (1 = a hit on that 16th): tresillos, offbeats, house chops. */
    constexpr uint16_t rhythms[] {
        0b1001001010010010, // 3-3-2-3-3-2
        0b1010010100101000, // 2-3-2-3...
        0b1000101000101010,
        0b1101001011010010,
        0b1001000110010010,
        0b1010101000100010,
    };
    bool hitAt (uint16_t rhythm, int k) noexcept { return ((rhythm >> (15 - (k % 16))) & 1u) != 0; }

    /** Euclid: `hits` spread as evenly as possible over 16 steps, turned by `rotation` (always a hit on step 0). */
    uint16_t euclid (int hits, int rotation) noexcept
    {
        hits = std::clamp (hits, 1, 16);
        uint16_t m = 0;
        for (int k = 0; k < 16; ++k)
            if (((k + rotation) * hits) % 16 < hits)
                m = (uint16_t) (m | (1u << (15 - k)));
        // the rotation moves the downbeat: put a hit back on step 0
        return (uint16_t) (m | 0x8000u);
    }

    /** A one-bar rhythm: a house template or a Euclidean one (3..9 hits, any rotation). */
    uint16_t randomRhythm (Random& r, int minHits, int maxHits) noexcept
    {
        if (r.chance (0.35))
            return rhythms[r.below ((int) (sizeof (rhythms) / sizeof (rhythms[0])))];
        return euclid (minHits + r.below (maxHits - minHits + 1), r.below (16));
    }

    /** Pitched slices land on the scale. */
    void snapToScale (const SourceInfo& src, Step& s) noexcept
    {
        if (s.pitch == 0 || ! pitched (src, s.slice))
            return;
        const float root = s.slice < (int) src.roots.size() ? src.roots[(size_t) s.slice] : -1.0f;
        const int base = root >= 0.0f ? (int) std::lround (root) : 60 + src.key.tonic;
        const auto scale = src.key.scale();
        auto inScale = [&] (int midi) { return std::find (scale.begin(), scale.end(), ((midi % 12) + 12) % 12) != scale.end(); };
        for (int d = 0; d <= 2; ++d) // the nearest note of the key (down first)
        {
            if (inScale (base + s.pitch - d))
            {
                s.pitch -= d;
                return;
            }
            if (inScale (base + s.pitch + d))
            {
                s.pitch += d;
                return;
            }
        }
    }

    int originalAt (const SourceInfo& src, int k) noexcept
    {
        const int n = (int) src.original.size();
        return n > 0 ? src.original[(size_t) (((k % n) + n) % n)] : -1;
    }

    /** The slice sounding at 16th `k` of the sample (the last one started at or before it). */
    int soundingAt (const SourceInfo& src, int k) noexcept
    {
        const int n = (int) src.original.size();
        for (int j = 0; j < n; ++j)
            if (const int s = originalAt (src, k - j); s >= 0)
                return s;
        return src.types.empty() ? -1 : 0;
    }

    /** The musical rules on the steps the dice changed (CHOP 0 stays the sample as it is). */
    void applyRules (Pattern& p, const SourceInfo& src, const std::vector<int>& base, const Pools& pools, Random& r)
    {
        const int n = p.numSteps();
        const bool melodic = p.style == Style::vocalChop || p.style == Style::pitchParty;
        for (int k = 0; k < n; ++k)
        {
            auto& s = p.steps[(size_t) k];
            if (isLocked (src, base[(size_t) k]))
                continue;
            const int inBar = k % Pattern::stepsPerBar;
            if (inBar == 0 && melodic)
            {
                // a melodic style: beat 1 plays its own line (no kick forced in)
                s.cut = false;
                if (s.slice < 0)
                    s.slice = base[(size_t) k] >= 0 ? base[(size_t) k] : soundingAt (src, k);
            }
            else if (inBar == 0)
            {
                // beat 1: a KICK, or the sample's own beat 1
                const int own = base[(size_t) k] >= 0 ? base[(size_t) k] : soundingAt (src, k);
                if (s.slice != own && roleAt (src, s.slice) != kickRole)
                {
                    const int kick = pick (pools.byRole[kickRole], r);
                    s.slice = kick >= 0 ? kick : own;
                }
                s.cut = false;
                s.reverse = false;
                if (s.slice < 0)
                    s.slice = own;
            }
            else if (! melodic && (inBar == 4 || inBar == 12) && s.slice >= 0 && s.slice != base[(size_t) k] && roleAt (src, s.slice) != snareRole
                     && ! pools.byRole[snareRole].empty() && r.chance (0.7))
            {
                // beats 2 and 4 lean on the snares
                const int own = base[(size_t) k];
                s.slice = own >= 0 && roleAt (src, own) == snareRole ? own : pick (pools.byRole[snareRole], r);
            }
            snapToScale (src, s);
        }
    }
} // namespace

const char* styleName (Style s) noexcept
{
    switch (s)
    {
        case Style::filterTouch: return "FILTER TOUCH";
        case Style::discoLoop: return "DISCO LOOP";
        case Style::vocalChop: return "VOCAL CHOP";
        case Style::stutterHouse: return "STUTTER HOUSE";
        case Style::reverseParty: return "REVERSE PARTY";
        case Style::pitchParty: break;
    }
    return "PITCH PARTY";
}

SourceInfo describe (const std::vector<Slice>& slices, const Key& key, double firstBeat, double sixteenth, int64_t numSamples,
                     const std::vector<bool>& locked)
{
    SourceInfo src;
    src.key = key;
    src.locked = locked;
    src.locked.resize (slices.size(), false);
    for (const auto& s : slices)
    {
        src.types.push_back (s.type);
        src.roots.push_back (s.rootMidi);
    }
    if (slices.empty() || sixteenth <= 0.0)
        return src;
    const auto steps = (int) std::floor (((double) numSamples - firstBeat) / sixteenth + 0.5);
    src.bars = std::max (1, steps / Pattern::stepsPerBar);
    src.original.assign ((size_t) src.bars * Pattern::stepsPerBar, -1);
    size_t next = 0;
    for (int k = 0; k < (int) src.original.size(); ++k)
    {
        const double t = firstBeat + k * sixteenth;
        // the slice starting nearest this 16th (within half a 16th)
        int best = -1;
        double bestD = sixteenth * 0.5;
        while (next < slices.size() && (double) slices[next].start < t - sixteenth * 0.5)
            ++next;
        for (size_t i = next; i < slices.size() && (double) slices[i].start < t + sixteenth * 0.5; ++i)
            if (const double d = std::abs ((double) slices[i].start - t); d < bestD)
            {
                bestD = d;
                best = (int) i;
            }
        src.original[(size_t) k] = best;
    }
    if (src.original[0] < 0)
    {
        // beat 1 must start something: the slice sounding there
        for (int i = (int) slices.size() - 1; i >= 0; --i)
            if ((double) slices[(size_t) i].start <= firstBeat)
            {
                src.original[0] = i;
                break;
            }
        if (src.original[0] < 0)
            src.original[0] = 0;
    }
    return src;
}

Pattern generate (const SourceInfo& src, Style style, float chopIn, uint32_t seed, int bars)
{
    Random r (0x5eedu ^ ((uint64_t) seed << 16) ^ ((uint64_t) style << 8));
    const double c = std::clamp ((double) chopIn, 0.0, 1.0);
    Pattern p;
    p.seed = seed;
    p.style = style;
    p.chop = (float) c;
    if (src.types.empty())
        return p;
    const Pools pools = poolsOf (src);

    if (bars <= 0)
    {
        switch (style)
        {
            case Style::filterTouch: bars = 1; break;
            case Style::discoLoop: bars = src.bars >= 4 ? 4 : src.bars >= 2 ? 2 : 1; break;
            case Style::pitchParty: bars = c < 0.4 ? 4 : 2; break;
            case Style::vocalChop:
            case Style::stutterHouse:
            case Style::reverseParty: bars = 2; break;
        }
        if (c > 0.85 && bars > 1)
            bars /= 2; // wild loops are short
    }
    bars = bars >= 4 ? 4 : bars >= 2 ? 2 : 1;
    p.bars = bars;
    const int n = bars * Pattern::stepsPerBar;
    p.steps.assign ((size_t) n, Step {});

    // where in the sample the loop starts (a whole number of loops in)
    const int total = (int) src.original.size();
    int from = 0;
    if (c > 0.05 && src.bars > bars)
        from = r.below (src.bars / bars) * bars * Pattern::stepsPerBar;

    // the base: the sample's own slices in their own order
    std::vector<int> base ((size_t) n, -1);
    for (int k = 0; k < n; ++k)
        base[(size_t) k] = originalAt (src, from + k);
    if (base[0] < 0)
        base[0] = soundingAt (src, from);

    const bool melodic = style == Style::vocalChop || style == Style::pitchParty;
    if (melodic)
        std::fill (base.begin(), base.end(), -1); // their own line, not the sample's order
    int phraseLen = n; // the steps the dice work on; FILTER TOUCH: its phrase, then repeated as it is

    switch (style)
    {
        case Style::filterTouch:
        {
            // a phrase of 1 or 2 beats, from a beat that starts with a hit, repeated (the French touch
            // gesture: the same short loop, the filter opening over 8 bars)
            phraseLen = c > 0.5 && r.chance (0.25) ? 2 : r.chance (0.5) ? 4 : 8;
            // the phrase starts on a beat (or an off-beat 8th) that has a hit
            std::vector<int> starts;
            for (int k = 0; k + phraseLen <= total; k += 2)
                if (originalAt (src, k) >= 0 && (k % 4 == 0 || r.chance (0.3)))
                    starts.push_back (k);
            auto startAt = [&] { return c < 0.05 || starts.empty() ? 0 : starts[(size_t) r.below ((int) starts.size())]; };
            const int start = startAt();
            for (int k = 0; k < phraseLen; ++k)
                base[(size_t) k] = originalAt (src, start + k);
            if (base[0] < 0)
                base[0] = soundingAt (src, start);
            for (int k = phraseLen; k < n; ++k)
                base[(size_t) k] = base[(size_t) (k % phraseLen)];
            // A A A B: the bar's last phrase from elsewhere (with CHOP)
            if (c > 0.3 && phraseLen < Pattern::stepsPerBar && r.chance (0.6))
            {
                const int other = startAt(), from2 = Pattern::stepsPerBar - phraseLen;
                for (int k = 0; k < phraseLen; ++k)
                    base[(size_t) (from2 + k)] = originalAt (src, other + k);
                if (base[(size_t) from2] < 0)
                    base[(size_t) from2] = soundingAt (src, other);
                phraseLen = Pattern::stepsPerBar; // the whole bar is the phrase now
                for (int k = phraseLen; k < n; ++k)
                    base[(size_t) k] = base[(size_t) (k % phraseLen)];
            }
            p.filterBars = 8;
            p.pump = true;
            break;
        }
        case Style::vocalChop:
        {
            // a one-bar rhythm, a small palette of voices (A A B A C...), the second bar answers the first
            const auto pal = palette (melodicPool (pools, false), 2 + (c > 0.4 ? 1 : 0) + (c > 0.75 ? 1 : 0), r);
            if (pal.empty())
                break;
            const auto rhythm = randomRhythm (r, 4, 8 + (c > 0.5 ? 1 : 0));
            // how the voices take turns: one of a few orders (A A B A, A B A C, A B B A...)
            static constexpr int orders[][8] { { 0, 0, 1, 0, 2, 1, 0, 3 }, { 0, 1, 0, 2, 0, 1, 0, 3 }, { 0, 1, 1, 0, 2, 2, 0, 3 },
                                               { 0, 0, 0, 1, 0, 0, 2, 3 }, { 0, 1, 2, 0, 1, 2, 3, 0 } };
            const auto& order = orders[r.below (5)];
            const int shift = r.below ((int) pal.size());
            int hit = 0;
            for (int k = 0; k < Pattern::stepsPerBar; ++k)
            {
                const bool extra = ! hitAt (rhythm, k) && k % 2 == 1 && r.chance (0.3 * c); // denser with CHOP: "ta-ta"
                if (! hitAt (rhythm, k) && ! extra)
                    continue;
                base[(size_t) k] = extra && k > 0 && base[(size_t) k - 1] >= 0 ? base[(size_t) k - 1] : pal[(size_t) ((order[hit % 8] + shift) % (int) pal.size())];
                ++hit;
            }
            if (base[0] < 0)
                base[0] = pal[0];
            for (int k = Pattern::stepsPerBar; k < n; ++k)
            {
                const int inBar = k % Pattern::stepsPerBar, bar = k / Pattern::stepsPerBar;
                int sl = base[(size_t) inBar];
                if (sl >= 0 && bar % 2 == 1 && inBar >= 8 && pal.size() > 1) // the answer: the second half, other voices
                    sl = pal[(size_t) ((std::find (pal.begin(), pal.end(), sl) - pal.begin() + 1) % (long) pal.size())];
                base[(size_t) k] = sl;
                if (sl >= 0 && bar % 2 == 1 && inBar >= 8 && c > 0.4 && pitched (src, sl))
                    p.steps[(size_t) k].pitch = scaleOffset (src, sl, 2); // a third up
            }
            break;
        }
        case Style::pitchParty:
        {
            // a melody from one tonal slice: a one-bar motif, the second bar answers and comes home
            const auto pool = melodicPool (pools, true);
            std::vector<int> withRoot;
            for (int i : pool)
                if (src.roots[(size_t) i] >= 0.0f)
                    withRoot.push_back (i);
            const int slice = pick (withRoot.empty() ? pool : withRoot, r);
            if (slice < 0)
                break;
            const auto rhythm = c < 0.25 && r.chance (0.5) ? (uint16_t) 0b1010101010101010 : randomRhythm (r, 5, 9);
            // the melody's shape: a walk, a rise, an arch or a fall
            std::vector<int> degrees;
            const int shape = r.below (4);
            int d = shape == 3 ? 5 : 0, hits = 0;
            for (int k = 0; k < Pattern::stepsPerBar; ++k)
                hits += hitAt (rhythm, k) || k == 0 ? 1 : 0;
            for (int i = 0; i < hits; ++i)
            {
                degrees.push_back (d);
                const int walk[] { -2, -1, 1, 1, 2, 3 };
                const int move = shape == 0 ? walk[r.below (6)]
                               : shape == 1 ? (r.chance (0.75) ? 1 + r.below (2) : -1)
                               : shape == 2 ? (i < hits / 2 ? 1 + r.below (2) : -1 - r.below (2))
                                            : (r.chance (0.75) ? -1 - r.below (2) : 1);
                d = std::clamp (d + move, -3, 7);
            }
            for (int k = 0; k < n; ++k)
            {
                const int inBar = k % Pattern::stepsPerBar, bar = k / Pattern::stepsPerBar;
                if (! hitAt (rhythm, inBar) && inBar != 0)
                    continue;
                int idx = 0;
                for (int j = 0; j < inBar; ++j)
                    idx += hitAt (rhythm, j) || j == 0 ? 1 : 0;
                int degree = degrees[(size_t) std::min (idx, (int) degrees.size() - 1)];
                if (bar % 2 == 1 && inBar >= 8)
                    degree = inBar >= 12 ? 0 : degree - 2;      // the answer comes home to the root
                if (bar >= 2 && c > 0.5 && inBar < 8)
                    degree += 2;                                 // bars 3-4 lift a little
                base[(size_t) k] = slice;
                p.steps[(size_t) k].pitch = scaleOffset (src, slice, degree);
                if (c > 0.6 && r.chance (0.12 * c))
                    p.steps[(size_t) k].repeat = 2;
            }
            break;
        }
        case Style::discoLoop:
        case Style::stutterHouse:
        case Style::reverseParty:
            break;
    }
    // beat juggling: whole beats of the sample (4 sixteenths) moved around, the way a chopper
    // rearranges a break; each beat keeps its own groove. Never a beat holding a locked slice.
    if (! melodic && style != Style::filterTouch && c > 0.05)
    {
        const std::vector<int> own = base; // the sample's order here, before any juggling
        std::vector<int> beats; // source beats that start with a hit and hold no locked slice
        for (int k = 0; k + 4 <= total; k += 4)
        {
            bool ok = originalAt (src, k) >= 0;
            for (int q = 0; q < 4 && ok; ++q)
                ok = ! isLocked (src, originalAt (src, k + q));
            if (ok)
                beats.push_back (k);
        }
        auto beatFree = [&] (int k0)
        {
            for (int q = 0; q < 4; ++q)
                if (isLocked (src, base[(size_t) (k0 + q)]))
                    return false;
            return true;
        };
        auto juggle = [&] (int barStart, double chance, int firstBeat)
        {
            if (beats.empty())
                return;
            for (int j = firstBeat; j < 4; ++j)
            {
                const int k0 = barStart + j * 4;
                if (! beatFree (k0) || ! r.chance (chance))
                    continue;
                // beat 1 only takes another bar's beat 1 (the rule: a kick, or the sample's own beat 1)
                std::vector<int> pool;
                for (int b : beats)
                    if (j != 0 || b % Pattern::stepsPerBar == 0)
                        pool.push_back (b);
                if (pool.empty())
                    continue;
                const int from0 = pool[(size_t) r.below ((int) pool.size())];
                for (int q = 0; q < 4; ++q)
                    base[(size_t) (k0 + q)] = originalAt (src, from0 + q);
            }
        };
        const double pj = 0.12 + 0.6 * smooth (0.1, 0.75, c);
        const bool faithful = style == Style::discoLoop && c <= 0.5;
        if (faithful)
            juggle (n - Pattern::stepsPerBar, pj, 1); // DISCO LOOP: its own bars in order, the last one varied
        else
        {
            // bar A, and a form over the bars (A B, A A A B, A B A B, A A B A, A B A C)
            juggle (0, pj, c > 0.6 ? 0 : 1);
            const std::vector<int> barA (base.begin(), base.begin() + Pattern::stepsPerBar);
            auto variant = [&] (int bar, double extra)
            {
                std::copy (barA.begin(), barA.end(), base.begin() + bar * Pattern::stepsPerBar);
                juggle (bar * Pattern::stepsPerBar, extra, 2); // beats 3 and 4 changed: a turnaround
            };
            static constexpr const char* forms4[] { "AAAB", "ABAB", "AABA", "ABAC", "AAAC" };
            const char* form = bars == 4 ? forms4[r.below (5)] : bars == 2 ? (r.chance (0.8) ? "AB" : "AA") : "A";
            for (int b = 1; b < bars; ++b)
            {
                if (form[b] == 'A')
                    std::copy (barA.begin(), barA.end(), base.begin() + b * Pattern::stepsPerBar);
                else
                    variant (b, form[b] == 'C' ? 0.9 : 0.6);
            }
        }
        // locked slices: back on their own steps, and taken off any other (a copied bar may carry them)
        for (int k = 0; k < n; ++k)
            if (isLocked (src, own[(size_t) k]) || isLocked (src, base[(size_t) k]))
                base[(size_t) k] = own[(size_t) k];
        if (base[0] < 0)
            base[0] = soundingAt (src, from);
    }
    for (int k = 0; k < n; ++k)
        p.steps[(size_t) k].slice = base[(size_t) k];

    std::vector<bool> frozen ((size_t) n);
    for (int k = 0; k < n; ++k)
        frozen[(size_t) k] = isLocked (src, base[(size_t) k]);

    // how much the dice change (DISCO LOOP: only its last bar)
    const bool faithful = style == Style::discoLoop;
    auto mutable_ = [&] (int k) { return k < phraseLen && ! frozen[(size_t) k] && (! faithful || k >= n - Pattern::stepsPerBar); };
    const double amount = faithful ? 0.25 + 0.3 * c : 1.0;

    if (c > 0.02 && ! melodic)
    {
        for (int k = 0; k < phraseLen; ++k)
        {
            if (! mutable_ (k))
                continue;
            auto& s = p.steps[(size_t) k];
            const int inBar = k % Pattern::stepsPerBar;
            // a few swaps for one of the same kind
            if (s.slice >= 0 && r.chance (amount * (0.1 + 0.45 * smooth (0.05, 0.6, c))))
                if (const int other = pick (pools.byRole[roleAt (src, s.slice)], r); other >= 0)
                    s.slice = other;
            // repeats on the strong beats (beat 3 takes beat 1's slice)
            if (inBar == 8 && r.chance (amount * 0.35 * smooth (0.15, 0.4, c)))
                s.slice = p.steps[(size_t) (k - 8)].slice;
            // stutters: 1/32 (or 1/64 when wild), or the 16th repeated
            if (s.slice >= 0 && r.chance (amount * 0.25 * smooth (0.3, 0.8, c)))
            {
                s.repeat = c > 0.85 && r.chance (0.5) ? 4 : 2;
                if (k + 1 < phraseLen && r.chance (0.4) && mutable_ (k + 1))
                    p.steps[(size_t) k + 1].slice = s.slice;
            }
            // a little reverse
            if (s.slice >= 0 && r.chance (amount * 0.2 * smooth (0.4, 1.0, c)))
                s.reverse = true;
            // tonal slices pitched on the scale
            if (pitched (src, s.slice) && r.chance (amount * 0.6 * smooth (0.6, 1.0, c)))
                s.pitch = scaleOffset (src, s.slice, r.below (9) - 2);
            // syncopation: an 8th pushed onto the next 16th
            if (s.slice >= 0 && inBar % 2 == 0 && inBar != 0 && k + 1 < phraseLen && p.steps[(size_t) k + 1].slice < 0 && mutable_ (k + 1)
                && r.chance (amount * 0.3 * smooth (0.6, 1.0, c)))
            {
                std::swap (s, p.steps[(size_t) k + 1]);
                continue;
            }
            // wild: any slice, fills, rhythmic cuts
            if (s.slice >= 0 && r.chance (amount * 0.4 * smooth (0.8, 1.0, c)))
                s.slice = pick (pools.all, r);
            if (s.slice < 0 && r.chance (amount * 0.2 * smooth (0.5, 1.0, c)))
            {
                s.slice = pick (pools.all, r);
                s.velocity = 0.8f;
            }
            if (inBar % 4 != 0 && r.chance (amount * 0.15 * smooth (0.85, 1.0, c)))
            {
                s = {};
                s.cut = true;
            }
        }
        // FILTER TOUCH: the phrase, changed once, repeated as it is
        for (int k = phraseLen; k < n; ++k)
            p.steps[(size_t) k] = p.steps[(size_t) (k % phraseLen)];
        // the style's own gesture
        if (style == Style::stutterHouse)
            for (int bar = 0; bar < bars; ++bar)
            {
                // the end of the bar (or its middle), the same slice, faster and faster
                const int at[] { 12, 12, 14, 10, 8 };
                const int k0 = bar * Pattern::stepsPerBar + at[r.below (bar % 2 == 1 ? 5 : 3)];
                const int len = Pattern::stepsPerBar - k0 % Pattern::stepsPerBar;
                if (frozen[(size_t) k0])
                    continue;
                const int s = p.steps[(size_t) k0].slice >= 0 ? p.steps[(size_t) k0].slice : soundingAt (src, from + k0);
                for (int j = 0; j < len; ++j)
                    if (! frozen[(size_t) k0 + (size_t) j])
                    {
                        const int rep = j < len / 2 ? (j == 0 ? 1 : 2) : (c > 0.6 && j == len - 1 ? 4 : 2);
                        p.steps[(size_t) k0 + (size_t) j] = { s, p.steps[(size_t) k0].pitch, rep, false, false, j == 0 ? 1.0f : 0.85f };
                    }
            }
    }
    if (style == Style::reverseParty)
    {
        // one slice in four reversed (which one: the seed's), or a whole beat backwards
        int count = 0;
        const int phase = r.below (4);
        for (int k = 0; k < n; ++k)
            if (p.steps[(size_t) k].slice >= 0 && ! frozen[(size_t) k] && count++ % 4 == phase && k % Pattern::stepsPerBar != 0)
                p.steps[(size_t) k].reverse = true;
        if (c > 0.4)
            for (int bar = 0; bar < bars; ++bar)
                if (r.chance (0.5))
                    for (int q = 0; q < 4; ++q)
                        if (auto& st = p.steps[(size_t) (bar * Pattern::stepsPerBar + 8 + q)]; st.slice >= 0 && ! frozen[(size_t) (bar * Pattern::stepsPerBar + 8 + q)])
                            st.reverse = true;
    }
    if (c > 0.02 || melodic)
        applyRules (p, src, base, pools, r);

    // the end relaunches the loop: the sample's last slice before the bar line, or a stutter
    // (not the melodic styles, which come home themselves, nor FILTER TOUCH's plain repeat)
    if (c > 0.02 && ! melodic && (style != Style::filterTouch || c > 0.5))
    {
        const int last = n - 1;
        if (! frozen[(size_t) last])
        {
            if (r.chance (0.5) && style != Style::filterTouch)
            {
                const int s = soundingAt (src, from + n - 1);
                p.steps[(size_t) last] = { s, 0, 1, false, false, 1.0f };
            }
            else
            {
                const int s = p.steps[(size_t) last - 1].slice >= 0 ? p.steps[(size_t) last - 1].slice : soundingAt (src, from + n - 2);
                p.steps[(size_t) last] = { s, p.steps[(size_t) last - 1].pitch, 2, false, false, 0.9f };
            }
        }
    }
    return p;
}

Pattern mutate (const Pattern& in, const SourceInfo& src, uint32_t seed)
{
    Pattern p = in;
    p.seed = seed;
    const int n = p.numSteps();
    if (n == 0 || src.types.empty())
        return p;
    Random r (0xe7c0deull ^ ((uint64_t) seed << 20));
    const Pools pools = poolsOf (src);
    const double share = 0.10 + 0.15 * r.uniform();
    const int count = std::max (1, (int) std::lround (share * n));
    std::vector<int> order ((size_t) n);
    for (int k = 0; k < n; ++k)
        order[(size_t) k] = k;
    for (int i = 0; i < count && i < n; ++i)
        std::swap (order[(size_t) i], order[(size_t) (i + r.below (n - i))]);
    std::vector<int> base ((size_t) n);
    for (int k = 0; k < n; ++k)
        base[(size_t) k] = in.steps[(size_t) k].slice;
    for (int i = 0; i < count && i < n; ++i)
    {
        const int k = order[(size_t) i];
        auto& s = p.steps[(size_t) k];
        if (isLocked (src, s.slice))
            continue;
        switch (r.below (5))
        {
            case 0:
                if (s.slice >= 0)
                    if (const int other = pick (pools.byRole[roleAt (src, s.slice)], r); other >= 0)
                        s.slice = other;
                break;
            case 1:
                if (s.slice < 0)
                {
                    s.slice = pick (pools.all, r);
                    s.cut = false;
                    s.velocity = 0.85f;
                }
                else if (k % 4 != 0)
                    s = {};
                break;
            case 2:
                if (s.slice >= 0)
                    s.repeat = s.repeat == 1 ? 2 : 1;
                break;
            case 3:
                if (s.slice >= 0 && r.chance (0.5))
                    s.reverse = ! s.reverse;
                break;
            default:
                if (pitched (src, s.slice))
                    s.pitch = std::clamp (s.pitch + (r.chance (0.5) ? 2 : -2), -12, 12);
                break;
        }
    }
    // beat 1 still holds (a mutation never takes it off)
    for (int k = 0; k < n; k += Pattern::stepsPerBar)
        if (p.steps[(size_t) k].slice < 0 || p.steps[(size_t) k].cut)
            p.steps[(size_t) k] = in.steps[(size_t) k];
    for (auto& s : p.steps)
        snapToScale (src, s);
    return p;
}
} // namespace daftchunk
