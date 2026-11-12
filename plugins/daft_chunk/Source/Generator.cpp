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

    /** The semitone offset that puts slice `slice` on scale degree `degree` (any octave, near 0). */
    int scaleOffset (const SourceInfo& src, int slice, int degree) noexcept
    {
        const auto scale = src.key.scale();
        const int octave = (int) std::floor (degree / 7.0);
        const int d = ((degree % 7) + 7) % 7;
        const int target = scale[(size_t) d] + 12 * octave; // a pitch class (+ octaves)
        const float root = slice >= 0 && slice < (int) src.roots.size() ? src.roots[(size_t) slice] : -1.0f;
        const int rootPc = root >= 0.0f ? ((int) std::lround (root) % 12 + 12) % 12 : src.key.tonic;
        int off = target - rootPc;
        while (off > 12)
            off -= 12;
        while (off < -12)
            off += 12;
        return off;
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
        for (int k = 0; k < n; ++k)
        {
            auto& s = p.steps[(size_t) k];
            if (isLocked (src, base[(size_t) k]))
                continue;
            const int inBar = k % Pattern::stepsPerBar;
            if (inBar == 0)
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
            else if ((inBar == 4 || inBar == 12) && s.slice >= 0 && s.slice != base[(size_t) k] && roleAt (src, s.slice) != snareRole
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

    switch (style)
    {
        case Style::filterTouch:
        {
            // a phrase of 1 or 2 beats, repeated (the French touch gesture); the filter opens over 8 bars
            const int len = r.chance (0.5) ? 4 : 8;
            const int beats = std::max (1, total / 4 - len / 4 + 1);
            const int start = (c < 0.05 ? 0 : r.below (beats)) * 4;
            for (int k = 0; k < n; ++k)
            {
                int s = originalAt (src, start + k % len);
                if (k % len == 0 && s < 0)
                    s = soundingAt (src, start);
                base[(size_t) k] = s;
            }
            p.filterBars = 8;
            p.pump = true;
            break;
        }
        case Style::vocalChop:
        {
            const auto& pool = ! pools.voice.empty() ? pools.voice : ! pools.tonal.empty() ? pools.tonal : pools.byRole[otherRole].empty() ? pools.all : pools.byRole[otherRole];
            for (int k = 0; k < n; ++k)
            {
                const bool eighth = k % 2 == 0;
                base[(size_t) k] = r.chance (eighth ? 0.75 : 0.2 + 0.3 * c) ? pick (pool, r) : -1;
            }
            for (int k = 0; k < n; k += Pattern::stepsPerBar) // beat 1 of every bar plays
                if (base[(size_t) k] < 0)
                    base[(size_t) k] = pick (pool, r);
            break;
        }
        case Style::pitchParty:
        {
            // a melody from one tonal slice
            const auto& pool = ! pools.tonal.empty() ? pools.tonal : ! pools.voice.empty() ? pools.voice : pools.all;
            const int slice = pick (pool, r);
            int degree = 0;
            for (int k = 0; k < n; ++k)
            {
                if (! r.chance (k % 2 == 0 ? 0.8 : 0.15 + 0.25 * c) && k % Pattern::stepsPerBar != 0)
                    continue;
                base[(size_t) k] = slice;
                p.steps[(size_t) k].pitch = scaleOffset (src, slice, degree);
                degree = std::clamp (degree + r.below (5) - 2, -4, 9); // a walk on the scale
            }
            break;
        }
        case Style::discoLoop:
        case Style::stutterHouse:
        case Style::reverseParty:
            break;
    }
    for (int k = 0; k < n; ++k)
        p.steps[(size_t) k].slice = base[(size_t) k];

    std::vector<bool> frozen ((size_t) n);
    for (int k = 0; k < n; ++k)
        frozen[(size_t) k] = isLocked (src, base[(size_t) k]);

    // how much the dice change (DISCO LOOP: only its last bar)
    const bool faithful = style == Style::discoLoop;
    auto mutable_ = [&] (int k) { return ! frozen[(size_t) k] && (! faithful || k >= n - Pattern::stepsPerBar); };
    const double amount = faithful ? 0.25 + 0.3 * c : 1.0;

    if (c > 0.02)
    {
        for (int k = 0; k < n; ++k)
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
                if (k + 1 < n && r.chance (0.4) && mutable_ (k + 1))
                    p.steps[(size_t) k + 1].slice = s.slice;
            }
            // a little reverse
            if (s.slice >= 0 && r.chance (amount * 0.2 * smooth (0.4, 1.0, c)))
                s.reverse = true;
            // tonal slices pitched on the scale
            if (pitched (src, s.slice) && r.chance (amount * 0.6 * smooth (0.6, 1.0, c)))
                s.pitch = scaleOffset (src, s.slice, r.below (12) - 4);
            // syncopation: an 8th pushed onto the next 16th
            if (s.slice >= 0 && inBar % 2 == 0 && inBar != 0 && k + 1 < n && p.steps[(size_t) k + 1].slice < 0 && mutable_ (k + 1)
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
        // the style's own gesture
        if (style == Style::stutterHouse)
            for (int bar = 0; bar < bars; ++bar)
            {
                const int k0 = bar * Pattern::stepsPerBar + 12; // the last beat: the same slice, faster and faster
                if (frozen[(size_t) k0])
                    continue;
                const int s = p.steps[(size_t) k0].slice >= 0 ? p.steps[(size_t) k0].slice : soundingAt (src, from + k0);
                const int reps[4] { 1, 2, 2, c > 0.6 ? 4 : 2 };
                for (int j = 0; j < 4; ++j)
                    if (! frozen[(size_t) k0 + (size_t) j])
                        p.steps[(size_t) k0 + (size_t) j] = { s, p.steps[(size_t) k0].pitch, reps[j], false, false, j == 0 ? 1.0f : 0.85f };
            }
        if (style == Style::reverseParty)
        {
            int count = 0;
            for (int k = 0; k < n; ++k)
                if (p.steps[(size_t) k].slice >= 0 && ! frozen[(size_t) k] && count++ % 4 == 3)
                    p.steps[(size_t) k].reverse = true;
        }
        applyRules (p, src, base, pools, r);

        // the end relaunches the loop: the sample's last slice before the bar line, or a stutter
        const int last = n - 1;
        if (! frozen[(size_t) last])
        {
            if (r.chance (0.5))
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
    else if (style == Style::reverseParty)
    {
        int count = 0;
        for (int k = 0; k < n; ++k)
            if (p.steps[(size_t) k].slice >= 0 && ! frozen[(size_t) k] && count++ % 4 == 3)
                p.steps[(size_t) k].reverse = true;
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
